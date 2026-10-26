#!/usr/bin/env python3
"""Compare both production radial recipes on WebGL2/SwiftShader with an independent reference."""
import argparse
from pathlib import Path
from playwright.sync_api import sync_playwright

parser = argparse.ArgumentParser()
parser.add_argument('--executable', default='/Applications/Google Chrome.app/Contents/MacOS/Google Chrome')
args = parser.parse_args()
recipes = []
for stage in ('vs', 'fs'):
    source = Path(f'assets/shaders/waterfront_water.{stage}').read_text()
    recipes.append(source.split('// BEGIN RADIAL RECIPE')[1].split('// END RADIAL RECIPE')[0])
with sync_playwright() as p:
    browser = p.chromium.launch(executable_path=args.executable, headless=True,
                               args=['--use-angle=swiftshader', '--enable-unsafe-swiftshader'])
    page = browser.new_page()
    result = page.evaluate('''recipes => {
        const gl = new OffscreenCanvas(1, 1).getContext('webgl2');
        if (!gl || !gl.getExtension('EXT_color_buffer_float')) throw Error('Float WebGL2 unavailable');
        const texture=gl.createTexture(); gl.bindTexture(gl.TEXTURE_2D,texture);
        gl.texImage2D(gl.TEXTURE_2D,0,gl.RGBA32F,1,1,0,gl.RGBA,gl.FLOAT,null);
        const fbo=gl.createFramebuffer(); gl.bindFramebuffer(gl.FRAMEBUFFER,fbo);
        gl.framebufferTexture2D(gl.FRAMEBUFFER,gl.COLOR_ATTACHMENT0,gl.TEXTURE_2D,texture,0);
        if(gl.checkFramebufferStatus(gl.FRAMEBUFFER)!==gl.FRAMEBUFFER_COMPLETE) throw Error('FBO incomplete');
        function shader(type, source) {
            const s=gl.createShader(type); gl.shaderSource(s,source); gl.compileShader(s);
            if(!gl.getShaderParameter(s,gl.COMPILE_STATUS)) throw Error(gl.getShaderInfoLog(s)); return s;
        }
        const results=[];
        for(const recipe of recipes) {
            const program=gl.createProgram();
            gl.attachShader(program,shader(gl.VERTEX_SHADER,`#version 300 es
                void main(){vec2 p=vec2((gl_VertexID<<1)&2,gl_VertexID&2); gl_Position=vec4(2.0*p-1.0,0,1);}`));
            gl.attachShader(program,shader(gl.FRAGMENT_SHADER,`#version 300 es
                precision highp float; out vec4 color; uniform vec2 probe;
                ${recipe}\nvoid main(){color=radialWave(probe);}`));
            gl.linkProgram(program);
            if(!gl.getProgramParameter(program,gl.LINK_STATUS)) throw Error(gl.getProgramInfoLog(program));
            gl.useProgram(program); gl.viewport(0,0,1,1);
            let maximum=0, samples=0;
            const values=new Float32Array(4);
            for(let step=0;step<=32;step+=2) {
                const radius=step;
                gl.uniform4fv(gl.getUniformLocation(program,'radialOrigin'),new Float32Array([-3,-7,radius, radius<30 ? 2 : 0, 0,0,0,0,0,0,0,0,0,0,0,0]));
                gl.uniform4fv(gl.getUniformLocation(program,'radialMotion'),new Float32Array([10,30,0,0,0,0,0,0,0,0,0,0,0,0,0,0]));
                for(let z=-25;z<=25;z+=4) for(let x=-25;x<=25;x+=4) {
                    const px=x+.173, pz=z+.391, dx=px-3, dz=pz+7;
                    const d=Math.hypot(dx,dz), shape=(d-radius)/2;
                    let expected=[0,0,0,0];
                    if(radius<30 && Math.abs(shape)<1) {
                        const q=1-shape*shape, fade=1-radius/30;
                        const slope=-4*fade*fade*shape*q;
                        expected=[2*fade*fade*q*q,-slope*dx/d,slope*dz/d,-10*slope-4*fade*10/30*q*q];
                    }
                    gl.uniform2f(gl.getUniformLocation(program,'probe'),-px,pz);
                    gl.drawArrays(gl.TRIANGLES,0,3); gl.readPixels(0,0,1,1,gl.RGBA,gl.FLOAT,values);
                    for(let j=0;j<4;++j) {
                        if(!Number.isFinite(values[j])) throw Error('nonfinite result');
                        maximum=Math.max(maximum,Math.abs(values[j]-expected[j]));
                    }
                    ++samples;
                }
            }
            if(maximum>.0002) throw Error(`radial error ${maximum}`);
            results.push({samples, maximum});
        }
        return results;
    }''', recipes)
    print('web_combat_wave=ok', result)
    browser.close()
