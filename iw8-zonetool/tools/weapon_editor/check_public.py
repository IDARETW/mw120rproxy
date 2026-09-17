"""Exercise a prepared public editor without changing the owner's projects."""
import argparse
import base64
import json
from pathlib import Path
import tempfile
import threading
import time
from urllib.request import Request, urlopen
from urllib.error import HTTPError

from server import Workbench, Handler, ThreadingHTTPServer, safe
from material import generate


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--config', type=Path, required=True)
    args = parser.parse_args()
    config = json.loads(args.config.read_text(encoding='utf-8-sig'))
    check_root = Path(config['workspace']).parent/'checks'
    check_root.mkdir(exist_ok=True)
    with tempfile.TemporaryDirectory(prefix='public-', dir=check_root) as temporary:
        config['workspace'] = temporary
        app = Workbench(config)
        assert app.list_projects() == [], 'Fresh installs must not depend on a private sample'
        server = ThreadingHTTPServer(('127.0.0.1', 0), Handler)
        server.app = app
        threading.Thread(target=server.serve_forever, daemon=True).start()
        base = 'http://127.0.0.1:' + str(server.server_port)

        def request(path, payload=None, **headers):
            data = json.dumps(payload).encode() if payload is not None else None
            if data:
                headers.update({'Content-Type':'application/json', 'X-Replay-Editor':'1'})
            try:
                with urlopen(Request(base+path, data=data, headers=headers), timeout=60) as response:
                    return response.status, response.read()
            except HTTPError as error:
                return error.code, error.read()

        try:
            code, body = request('/api/bootstrap')
            boot = json.loads(body)
            assert code == 200 and not boot['protected'] and boot['projects'] == []
            assert 'server_file_roots' not in boot
            for route in ('/', '/web-missing', '/api/server-files', '/api/server-files/content?root=downloads&path=test.obj'):
                status, _ = request(route)
                assert status == (200 if route == '/' else 404), (route, status)
            assert request('/api/bootstrap', Host='example.invalid')[0] == 400
            payload = {'reference':'iw8_pi_mike1911_mp','title':'Public setup check'}
            assert request('/api/projects', payload, Origin='https://example.invalid')[0] == 400
            status, body = request('/api/projects', payload)
            assert status == 200, body
            project = json.loads(body)
            assert not project['model'] and not project.get('stock_reference')
            root = app.project_path(project['id'])
            obj = b'v 0 0 0\nv 1 0 0\nv 0 1 0\nvt 0 0\nvt 1 0\nvt 0 1\nf 1/1 2/2 3/3\n'
            status, body = request('/api/projects/'+project['id']+'/upload', {'name':'check.obj','data':base64.b64encode(obj).decode()})
            assert status == 200, body
            assert (root/json.loads(body)['path']).read_bytes() == obj
            try:
                safe(root, '../outside.txt')
            except ValueError:
                pass
            else:
                raise AssertionError('Project path traversal accepted')
            definition = generate(root, {'color':'#4488cc','roughness':.5}, app.library/'material/material.json')
            material = json.loads((root/definition).read_text())
            assert material['format'] == 'replay-weapon-material-v1'
            assert len(material['images']) == 3
            preview = app.animation_preview(project)
            available = [event['asset'] for package in preview['packages'] for event in package['events'] if event['available']]
            assert available, 'No local native animation payloads'
            clip = app.animation_preview(project, available[0])
            assert clip['tracks'], 'Native clip has no decoded tracks'
            status, body = request('/api/projects/'+project['id']+'/build', {})
            assert status == 200, body
            job = app.jobs[json.loads(body)['id']]
            deadline = time.monotonic()+620
            while job['status'] not in ('succeeded', 'failed') and time.monotonic()<deadline:
                time.sleep(.1)
            assert job['status'] == 'succeeded', job['log']
            assert sum(file['name'].endswith('.ff') for file in job['files']) == 8
            print(json.dumps({'status':'passed','weapons':len(app.catalog['weapons']),
                'attachments':len(app.catalog['attachments']), 'local_upload':True,
                'server_file_routes_removed':True, 'local_session':True,
                'material_generated':True,'animation_tracks':len(clip['tracks']),
                'native_companion_fastfiles':8}))
        finally:
            server.shutdown()
            server.server_close()


if __name__ == '__main__':
    main()
