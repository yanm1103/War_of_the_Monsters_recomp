"""OBS control over obs-websocket (v5) for recording tutorial takes.

The password is read from OBS's own config (%APPDATA%/obs-studio/plugin_config/obs-websocket/config.json),
so it never lands in the repo.

    python tools/tutorial/obsctl.py status
    python tools/tutorial/obsctl.py start
    python tools/tutorial/obsctl.py stop      # prints the recorded file path
"""
import json
import os
import sys

import obsws_python as obs


def client():
    cfg = os.path.join(os.environ['APPDATA'], 'obs-studio', 'plugin_config', 'obs-websocket', 'config.json')
    with open(cfg, encoding='utf-8') as f:
        c = json.load(f)
    return obs.ReqClient(host='localhost', port=c.get('server_port', 4455),
                         password=c.get('server_password', ''), timeout=5)


def main():
    cmd = sys.argv[1] if len(sys.argv) > 1 else 'status'
    cl = client()
    if cmd == 'status':
        st = cl.get_record_status()
        print('recording' if st.output_active else 'idle', st.output_timecode)
        print('scene:', cl.get_current_program_scene().scene_name)
        v = cl.get_video_settings()
        print('canvas %dx%d out %dx%d fps %s/%s' % (v.base_width, v.base_height, v.output_width, v.output_height,
                                                    v.fps_numerator, v.fps_denominator))
        print('dir:', cl.get_record_directory().record_directory)
    elif cmd == 'start':
        cl.start_record()
        print('started')
    elif cmd == 'stop':
        r = cl.stop_record()
        print(r.output_path)
    else:
        sys.exit('usage: obsctl.py status|start|stop')


if __name__ == '__main__':
    main()
