"""Pack source PBR maps into Replay's verified single-UV weapon profile."""
import copy
import gzip
import json
from pathlib import Path
import uuid

from PIL import Image, ImageChops, ImageOps


def ensure_previews(definition_path, max_dimension=1024):
    """Add small, losslessly transported editor textures; native RGBA stays intact."""
    definition_path = Path(definition_path)
    definition = json.loads(definition_path.read_text(encoding='utf-8'))
    if definition.get('format') != 'replay-weapon-material-v1':
        return
    previews = []
    for image in definition['images']:
        source = definition_path.parent / image['file']
        target = source.with_name(source.name + '.preview.gz')
        width, height = image['width'], image['height']
        scale = min(1, max_dimension / max(width, height))
        preview_size = (max(1, round(width * scale)), max(1, round(height * scale)))
        if not target.is_file() or target.stat().st_mtime_ns < source.stat().st_mtime_ns:
            pixels = Image.frombytes('RGBA', (width, height), source.read_bytes())
            if pixels.size != preview_size:
                pixels = pixels.resize(preview_size, Image.Resampling.BOX)
            target.write_bytes(gzip.compress(pixels.tobytes(), compresslevel=6, mtime=0))
        previews.append({'file': target.name, 'width': preview_size[0],
                         'height': preview_size[1], 'encoding': 'gzip'})
    if definition.get('preview_images') != previews:
        definition['preview_images'] = previews
        temporary = definition_path.with_name(definition_path.name + '.tmp')
        temporary.write_text(json.dumps(definition, indent=2), encoding='utf-8')
        temporary.replace(definition_path)


def generate(root, settings, profile, folder_name=None):
    root=Path(root).resolve()
    def local(name):
        path=(root/name).resolve()
        if not path.is_relative_to(root):
            raise ValueError('Texture source must be inside its project')
        return path
    original=settings.get('source_definition',settings.get('definition'))
    source=local(original) if original else Path(profile)
    definition=json.loads(source.read_text())
    if definition.get('format')!='replay-weapon-material-v1':
        raise ValueError('Expected the Replay single-UV material profile')

    maps=settings.get('maps') if isinstance(settings.get('maps'),dict) else settings
    def open_map(role):
        name=maps.get(role)
        if not name:
            return None
        with Image.open(local(name)) as image:
            return image.convert('RGBA')

    loaded_maps={role:open_map(role) for role in (
        'color_texture','normal_texture','roughness_texture','metallic_texture',
        'ao_texture','specular_texture','emissive_texture')}
    color=loaded_maps['color_texture']
    if color is None and original:
        image=definition['images'][0]
        color=Image.frombytes('RGBA',(image['width'],image['height']),
                              (source.parent/image['file']).read_bytes())
    candidates=[image for image in loaded_maps.values() if image is not None]
    if color is not None:
        candidates.append(color)
    target=max(candidates,key=lambda image:image.width*image.height).size if candidates else (1,1)
    if max(target)>2048:
        scale=2048/max(target)
        target=(max(1,round(target[0]*scale)),max(1,round(target[1]*scale)))
    color=color or Image.new('RGBA',target,(255,255,255,255))
    if color.size!=target:
        color=color.resize(target,Image.Resampling.LANCZOS)
    size=target
    def resized(role):
        image=loaded_maps.get(role)
        if image is None:
            return None
        if image.size!=size:
            image=image.resize(size,Image.Resampling.BILINEAR)
        return image
    color=ImageChops.multiply(color,Image.new('RGBA',size,settings.get('color','#ffffff')))
    ao=resized('ao_texture')
    if ao:
        mask=ImageOps.grayscale(ao)
        color=Image.merge('RGBA',tuple(ImageChops.multiply(channel,mask) for channel in color.split()[:3])+(color.getchannel('A'),))

    def factor(name,default):
        return max(0,min(1,float(settings.get(name,default))))
    specular=factor('specular',.22)
    metalness=factor('metalness',.0)
    specular_map=resized('specular_texture')
    metallic_map=resized('metallic_texture')
    spec=Image.new('L',size,round(specular*255))
    if specular_map:
        spec=ImageChops.multiply(spec,ImageOps.grayscale(specular_map))
    if metallic_map and metalness:
        metallic=ImageOps.grayscale(metallic_map).point(lambda value: round(value*metalness))
        spec=ImageChops.lighter(spec,metallic)
    color.putalpha(spec)

    roughness=factor('roughness',.45)
    normal=resized('normal_texture') or Image.new('RGBA',size,(128,128,255,255))
    roughness_map=resized('roughness_texture')
    if roughness_map:
        gloss=ImageOps.grayscale(roughness_map).point(lambda value: 255-value)
    else:
        gloss=Image.new('L',size,round((1-roughness)*255))
    normal.putalpha(gloss)
    emissive=resized('emissive_texture') or Image.new('RGBA',size,(0,0,0,255))

    folder=root/'assets'/(folder_name or ('material-'+uuid.uuid4().hex[:10]))
    folder.mkdir(parents=True,exist_ok=True)
    definition=copy.deepcopy(definition)
    for index,(name,image) in enumerate(zip(('color_specular','normal_gloss','emissive'),(color,normal,emissive))):
        (folder/(name+'.rgba')).write_bytes(image.tobytes())
        definition['images'][index]={'file':name+'.rgba','width':image.width,'height':image.height}
    (folder/'material.json').write_text(json.dumps(definition,indent=2))
    ensure_previews(folder/'material.json')
    return (folder/'material.json').relative_to(root).as_posix()
