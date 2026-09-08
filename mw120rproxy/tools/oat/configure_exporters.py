"""Install the version-pinned Replay dumpers into a local OpenAssetTools checkout."""
import argparse
from pathlib import Path
import shutil
import subprocess

PIN='7d027e8f89118196713e955b0e11f8404149c54d'
WAVELET='39e46495b9dfa0f533fed4ae72b175d99eb165d5'


def configure(root,with_wavelet=False):
    root=root.resolve()
    actual=subprocess.check_output(['git','rev-parse','HEAD'],cwd=root,text=True).strip()
    if actual!=PIN:raise ValueError(f'Expected OpenAssetTools v0.33.0 {PIN}, found {actual}')
    directory=root/'src/ObjWriting/Game/IW3';writer=directory/'ObjWriterIW3.cpp'
    text=writer.read_text()
    if '#include "ReplayMapDumpers.h"' not in text:
        anchor='#include "ObjWriterIW3.h"'
        if text.count(anchor)!=1:raise ValueError('Missing ObjWriter include anchor')
        text=text.replace(anchor,anchor+'\n#include "ReplayMapDumpers.h"')
    registrations=(('AssetDumperClipMap','replay_export::Collision<IW3::AssetClipMap>'),
        ('AssetDumperGfxWorld','replay_export::World'))
    for old,new in registrations:
        if f'std::make_unique<{new}>()' in text:continue
        anchor=f'// REGISTER_DUMPER({old})'
        if text.count(anchor)!=1:raise ValueError(f'Missing registration anchor {anchor}')
        text=text.replace(anchor,f'RegisterAssetDumper(std::make_unique<{new}>());')
    pvs='RegisterAssetDumper(std::make_unique<replay_export::Collision<IW3::AssetClipMapPvs>>());'
    if pvs not in text:
        anchor='RegisterAssetDumper(std::make_unique<replay_export::Collision<IW3::AssetClipMap>>());'
        text=text.replace(anchor,anchor+'\n    '+pvs)
    shutil.copyfile(Path(__file__).with_name('ReplayMapDumpers.h'),directory/'ReplayMapDumpers.h')
    writer.write_text(text)
    if with_wavelet:
        subprocess.run(['git','fetch','--depth=2','origin',WAVELET],cwd=root,check=True)
        for name in ('IwiLoader.cpp','IwiWaveletDecoder.cpp','IwiWaveletDecoder.h'):
            path='src/ObjImage/Image/'+name
            replacement=subprocess.check_output(['git','show',WAVELET+':'+path],cwd=root)
            destination=root/path
            if destination.exists() and destination.read_bytes()!=replacement:
                original=subprocess.run(['git','show',PIN+':'+path],cwd=root,capture_output=True)
                if original.returncode or destination.read_bytes().replace(b'\r\n',b'\n')!=original.stdout.replace(b'\r\n',b'\n'):
                    raise ValueError(f'Preserve unexpected local changes in {path}')
            destination.write_bytes(replacement)
    print(f'Configured Replay exporters in {root}; rebuild UnlinkerCli next.')


if __name__=='__main__':
    p=argparse.ArgumentParser(description=__doc__);p.add_argument('checkout',type=Path)
    p.add_argument('--with-wavelet',action='store_true')
    a=p.parse_args();configure(a.checkout,a.with_wavelet)
