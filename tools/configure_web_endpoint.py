#!/usr/bin/env python3
"""Set the published directory URL without changing the compiled release client."""
import argparse
import json
from pathlib import Path
import re
from urllib.parse import urlsplit


def configure_text(source, directory_url):
    address = urlsplit(directory_url)
    if (address.scheme != 'https' or not address.hostname or address.username
            or address.password or address.query or address.fragment
            or address.path != '/servers.json'
            or not re.fullmatch(r'[A-Za-z0-9.-]+', address.hostname)):
        raise ValueError('Use an HTTPS /servers.json URL without credentials or query parameters.')
    if address.port is not None and not 1 <= address.port <= 65535:
        raise ValueError('Invalid endpoint port.')
    replacement = json.dumps(directory_url).replace('<', '\\u003c')
    output, count = re.subn(r'(?m)^(\s*boatServerListUrl:)\s*.*$',
                            lambda match: match[1] + ' ' + replacement + ',', source)
    if count != 1:
        raise ValueError('Expected exactly one server-directory setting; client may be too old.')
    return output


def configure(index, directory_url):
    index.write_text(configure_text(index.read_text(), directory_url))


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--index', type=Path, required=True)
    parser.add_argument('--directory-url', required=True)
    args = parser.parse_args()
    configure(args.index, args.directory_url)


if __name__ == '__main__':
    main()
