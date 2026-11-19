"""The private basin tool must reject GPU before creating a window."""
import subprocess
import sys

result = subprocess.run([sys.argv[1], "--renderer", "gpu"],
                        capture_output=True, text=True, timeout=10)
assert result.returncode == 2, (result.returncode, result.stdout, result.stderr)
assert result.stderr.strip() == "Wake research basin requires --renderer cpu.", result.stderr
