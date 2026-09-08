"""Classify foliage alpha without interpreting opaque specular masks as opacity."""
import re

def classify(name,material,image):
    alpha=image.getchannel('A');hist=alpha.histogram();total=image.width*image.height
    has_cutout=sum(hist[:128])>total*.001 and sum(hist[128:])>total*.001
    foliage=bool(re.search(r'leaf|leaves|foliage|tree|bush|branch|grass|fern|pine|hedge|palm',name+' '+material.get('image',''),re.I))
    if material.get('alpha_test') or (foliage and has_cutout):
        material['cutout']=True;material['blended']=False
        material['alpha_reason']='authored_alpha_test' if material.get('alpha_test') else 'foliage_color_alpha'
    return material
