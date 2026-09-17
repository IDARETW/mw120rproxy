"""Author resident RGBA textures for the verified Replay single-UV profile."""
import copy
import json
from pathlib import Path
import uuid

from PIL import Image, ImageChops


def generate(root, settings, profile):
    root=Path(root).resolve()
    def local(name):
        path=(root/name).resolve()
        if not path.is_relative_to(root):
            raise ValueError('Texture source must be inside its project')
        return path
    original=settings.setdefault('source_definition',settings.get('definition'))
    source=local(original) if original else Path(profile)
    definition=json.loads(source.read_text())
    if definition.get('format')!='replay-weapon-material-v1':
        raise ValueError('Expected the Replay single-UV material profile')
    if settings.get('color_texture'):
        color=Image.open(local(settings['color_texture'])).convert('RGBA')
    elif original:
        image=definition['images'][0]
        color=Image.frombytes('RGBA',(image['width'],image['height']),
                              (source.parent/image['file']).read_bytes())
    else:
        color=Image.new('RGBA',(1,1),(255,255,255,255))
    if max(color.size)>2048:
        color.thumbnail((2048,2048),Image.Resampling.LANCZOS)
    color=ImageChops.multiply(color,Image.new('RGBA',color.size,settings.get('color','#ffffff')))
    specular=max(0,min(1,float(settings.get('specular',.22))))
    roughness=max(0,min(1,float(settings.get('roughness',.45))))
    color.putalpha(round(specular*255))
    if settings.get('normal_texture'):
        normal=Image.open(local(settings['normal_texture'])).convert('RGBA')
        if max(normal.size)>2048:
            normal.thumbnail((2048,2048),Image.Resampling.LANCZOS)
    else:
        normal=Image.new('RGBA',(1,1),(128,128,255,255))
    normal.putalpha(round((1-roughness)*255))
    emissive=Image.open(local(settings['emissive_texture'])).convert('RGBA') if settings.get('emissive_texture') else Image.new('RGBA',(1,1),(0,0,0,255))
    if max(emissive.size)>2048:
        emissive.thumbnail((2048,2048),Image.Resampling.LANCZOS)
    folder=root/'assets'/('material-'+uuid.uuid4().hex[:10])
    folder.mkdir(parents=True)
    definition=copy.deepcopy(definition)
    for index,(name,image) in enumerate(zip(('color_specular','normal_gloss','emissive'),(color,normal,emissive))):
        (folder/(name+'.rgba')).write_bytes(image.tobytes())
        definition['images'][index]={'file':name+'.rgba','width':image.width,'height':image.height}
    (folder/'material.json').write_text(json.dumps(definition,indent=2))
    return (folder/'material.json').relative_to(root).as_posix()
