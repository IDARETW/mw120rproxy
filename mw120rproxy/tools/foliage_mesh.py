"""Bake alpha-tested cards into geometry shared by color, depth and shadow passes.

This is a bounded binary coverage conversion, not alpha blending. Alpha-mask
contours are simplified with holes preserved, clipped to each source triangle,
then triangulated. Positions/UVs and winding follow the source mesh. It does not
require new Replay descriptor bindings.
"""
import math
from PIL import Image
from replay_mesh_math import pack,quaternion,unpack_normal


def mask_rectangles(image,size=128,threshold=128):
    w,h=image.size;factor=min(1,size/max(w,h));w=max(1,round(w*factor));h=max(1,round(h*factor))
    alpha=image.getchannel('A').resize((w,h),Image.Resampling.BOX)
    pixels=alpha.tobytes();active={};rects=[]
    for y in range(h):
        runs=[];x=0
        while x<w:
            if pixels[y*w+x]<threshold:x+=1;continue
            start=x
            while x<w and pixels[y*w+x]>=threshold:x+=1
            runs.append((start,x))
        next_active={}
        for run in runs:next_active[run]=(active.pop(run,(y,y))[0],y+1)
        for (x0,x1),(y0,y1) in active.items():rects.append((x0/w,y0/h,x1/w,y1/h))
        active=next_active
    for (x0,x1),(y0,y1) in active.items():rects.append((x0/w,y0/h,x1/w,y1/h))
    return rects,(w,h)


def reverse_normal(packed):
    n=[-x for x in unpack_normal(packed)];axis=[0,0,1] if abs(n[2])<.9 else [0,1,0]
    t=[axis[1]*n[2]-axis[2]*n[1],axis[2]*n[0]-axis[0]*n[2],axis[0]*n[1]-axis[1]*n[0]]
    b=[n[1]*t[2]-n[2]*t[1],n[2]*t[0]-n[0]*t[2],n[0]*t[1]-n[1]*t[0]]
    return pack(*quaternion(t,b,n))


def mask_geometry(rectangles,size):
    from pathlib import Path
    import sys
    vendor=str(Path(__file__).resolve().parent/'_vendor')
    if vendor not in sys.path:sys.path.insert(0,vendor)
    try:
        import shapely
    except ImportError as exc:
        raise RuntimeError('Install foliage build dependencies: python -m pip install -r requirements.txt') from exc
    geometry=shapely.union_all([shapely.box(*r) for r in rectangles])
    return shapely.simplify(geometry,.75/max(size),preserve_topology=True)


def collapsed_uv_polygons(tri,mask):
    """Clip real triangles whose UVs describe a point or line (e.g. plant stems)."""
    import shapely
    from shapely.affinity import translate
    axis=max(range(2),key=lambda k:max(v['uv'][k] for v in tri)-min(v['uv'][k] for v in tri))
    lo=min(tri,key=lambda v:v['uv'][axis]);hi=max(tri,key=lambda v:v['uv'][axis])
    if hi['uv'][axis]-lo['uv'][axis]<1e-10:
        return [tri] if mask.covers(shapely.Point(*(x%1 for x in lo['uv']))) else []
    line=shapely.LineString([lo['uv'],hi['uv']]);ranges=[]
    tx0,ty0=map(math.floor,line.bounds[:2]);tx1,ty1=map(math.ceil,line.bounds[2:])
    tx1=max(tx0+1,tx1);ty1=max(ty0+1,ty1)
    if (tx1-tx0)*(ty1-ty0)>64:raise ValueError('Alpha-tested triangle repeats its texture more than 64 times; split it')
    def segments(g):
        if g.geom_type=='LineString' and not g.is_empty:
            values=[p[axis] for p in g.coords];ranges.append((min(values),max(values)))
        elif hasattr(g,'geoms'):
            for part in g.geoms:segments(part)
    for ty in range(ty0,ty1):
        for tx in range(tx0,tx1):segments(line.intersection(translate(mask,xoff=tx,yoff=ty)))
    def clip(poly,bound,greater):
        out=[]
        if not poly:return out
        a=poly[-1];inside=lambda v:v['uv'][axis]>=bound if greater else v['uv'][axis]<=bound
        last=inside(a)
        for b in poly:
            current=inside(b)
            if current!=last:
                t=(bound-a['uv'][axis])/(b['uv'][axis]-a['uv'][axis])
                out.append({'position':[x+(y-x)*t for x,y in zip(a['position'],b['position'])],
                            'uv':[x+(y-x)*t for x,y in zip(a['uv'],b['uv'])],'normal':(a if t<.5 else b)['normal']})
            if current:out.append(b)
            a=b;last=current
        return out
    return [clip(clip(tri,a,True),b,False) for a,b in sorted(set(ranges)) if b-a>1e-10]


def cutout(surface,mask,two_sided=True,triangle_budget=250000):
    import shapely
    from shapely.affinity import translate
    output=[];vertices=[];indices=[];emitted=0
    def flush():
        nonlocal vertices,indices
        if indices:output.append({**surface,'vertices':vertices,'indices':indices})
        vertices=[];indices=[]
    for start in range(0,len(surface['indices']),3):
        tri=[surface['vertices'][i] for i in surface['indices'][start:start+3]]
        a,b,c=[v['position'] for v in tri]
        ab=[b[k]-a[k] for k in range(3)];ac=[c[k]-a[k] for k in range(3)]
        cross=[ab[1]*ac[2]-ab[2]*ac[1],ab[2]*ac[0]-ab[0]*ac[2],ab[0]*ac[1]-ab[1]*ac[0]]
        if sum(x*x for x in cross)<1e-12:continue
        us=[v['uv'][0] for v in tri];vs=[v['uv'][1] for v in tri]
        area=(us[1]-us[0])*(vs[2]-vs[0])-(vs[1]-vs[0])*(us[2]-us[0])
        if abs(area)<1e-12:
            for poly in collapsed_uv_polygons(tri,mask):
                for i in range(1,len(poly)-1):
                    batch=[poly[0],poly[i],poly[i+1]];count=2 if two_sided else 1
                    if emitted+count>triangle_budget:raise ValueError('Foliage geometry budget exceeded')
                    if len(vertices)+3*count>60000:flush()
                    for back in range(count):
                        for v in (list(reversed(batch)) if back else batch):
                            indices.append(len(vertices));vertices.append({**v,'normal':reverse_normal(v['normal'])} if back else v)
                    emitted+=count
            continue
        tx0,tx1,ty0,ty1=math.floor(min(us)),math.ceil(max(us)),math.floor(min(vs)),math.ceil(max(vs))
        if (tx1-tx0)*(ty1-ty0)>64:raise ValueError('Alpha-tested triangle repeats its texture more than 64 times; split it')
        uvtri=shapely.Polygon(zip(us,vs))
        for ty in range(ty0,ty1):
            for tx in range(tx0,tx1):
                clipped=uvtri.intersection(translate(mask,xoff=tx,yoff=ty))
                triangles=shapely.constrained_delaunay_triangles(clipped)
                for piece in triangles.geoms:
                    pts=list(piece.exterior.coords)[:3]
                    a,b,c=pts
                    signed=(b[0]-a[0])*(c[1]-a[1])-(b[1]-a[1])*(c[0]-a[0])
                    if abs(signed)<1e-12:continue
                    if signed*area<0:pts.reverse()
                    count=2 if two_sided else 1
                    if emitted+count>triangle_budget:raise ValueError('Foliage geometry budget exceeded; use a lower mask size or simpler foliage')
                    if len(vertices)+3*count>60000:flush()
                    batch=[]
                    for u,v in pts:
                        b1=((u-us[0])*(vs[2]-vs[0])-(v-vs[0])*(us[2]-us[0]))/area
                        b2=((us[1]-us[0])*(v-vs[0])-(vs[1]-vs[0])*(u-us[0]))/area
                        weights=[1-b1-b2,b1,b2]
                        vertex={'position':[sum(weights[j]*tri[j]['position'][k] for j in range(3)) for k in range(3)],
                                'uv':[u,v],'normal':tri[max(range(3),key=lambda j:weights[j])]['normal']}
                        batch.append(vertex)
                    for back in range(count):
                        ordered=list(reversed(batch)) if back else batch
                        for v in ordered:
                            indices.append(len(vertices));vertices.append({**v,'normal':reverse_normal(v['normal'])} if back else v)
                    emitted+=count
    flush()
    return output
