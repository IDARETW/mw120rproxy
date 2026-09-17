import * as THREE from 'three';
import { OrbitControls } from 'three/addons/controls/OrbitControls.js';
import { TransformControls } from 'three/addons/controls/TransformControls.js';
import { OBJLoader } from 'three/addons/loaders/OBJLoader.js';

const mat4 = rows => new THREE.Matrix4().set(...rows[0],...rows[1],...rows[2],0,0,0,1);

export async function readResource(url,onProgress){
  const chunks=[];let offset=0,total=0;
  do{
    const response=await fetch(url,{headers:{Range:`bytes=${offset}-${offset+4*1024*1024-1}`}});
    if(!response.ok)throw new Error(`Could not load model resource (${response.status})`);
    if(response.status!==206){if(offset)throw new Error('Incomplete model resource');return response.blob();}
    const range=/^bytes (\d+)-(\d+)\/(\d+)$/.exec(response.headers.get('Content-Range')||'');
    if(!range||Number(range[1])!==offset||Number(range[3])>512*1024*1024)throw new Error('Invalid model resource range');
    if(total&&total!==Number(range[3]))throw new Error('Model resource changed during download');
    total=Number(range[3]);const chunk=await response.blob();
    if(!chunk.size||chunk.size!==Number(range[2])-offset+1)throw new Error('Incomplete model resource');
    chunks.push(chunk);offset+=chunk.size;onProgress?.(offset,total);
  }while(offset<total);
  return new Blob(chunks);
}

export class WeaponViewer {
  constructor(element, onChange, onSelect, onTransform) {
    this.element=element; this.onChange=onChange; this.onSelect=onSelect; this.onTransform=onTransform;
    this.scene=new THREE.Scene(); this.scene.background=new THREE.Color('#f1f3f6');
    this.camera=new THREE.PerspectiveCamera(33,1,.01,5000); this.camera.up.set(0,0,1);
    this.renderer=new THREE.WebGLRenderer({antialias:true,preserveDrawingBuffer:true});
    this.renderer.setPixelRatio(Math.min(devicePixelRatio,2));
    this.renderer.shadowMap.enabled=true; this.renderer.shadowMap.type=THREE.PCFShadowMap;
    this.renderer.toneMapping=THREE.ACESFilmicToneMapping; this.renderer.toneMappingExposure=1.25;
    element.prepend(this.renderer.domElement);
    this.controls=new OrbitControls(this.camera,this.renderer.domElement);
    this.controls.enableDamping=true; this.controls.dampingFactor=.09;
    const hemi=new THREE.HemisphereLight(0xeaf1ff,0x9fa6b4,2.8); hemi.position.set(0,0,30); this.scene.add(hemi);
    const key=new THREE.DirectionalLight(0xffffff,4); key.position.set(-10,-15,25); key.castShadow=true;
    key.shadow.mapSize.set(2048,2048); Object.assign(key.shadow.camera,{left:-35,right:35,top:35,bottom:-35,near:.5,far:100});
    key.shadow.bias=-.0003; key.shadow.normalBias=.04; this.scene.add(key);
    const fill=new THREE.DirectionalLight(0xcbdfff,2); fill.position.set(12,8,10); this.scene.add(fill);
    this.grid=new THREE.GridHelper(100,100,0xd2d9e4,0xe3e7ee); this.grid.rotation.x=Math.PI/2;
    this.grid.material.transparent=true; this.grid.material.opacity=.65; this.scene.add(this.grid);
    this.ground=new THREE.Mesh(new THREE.PlaneGeometry(1000,1000),new THREE.ShadowMaterial({opacity:.1}));
    this.ground.receiveShadow=true; this.scene.add(this.ground);
    this.model=new THREE.Group();this.scene.add(this.model);this.template=new THREE.Group();this.template.name='reference_template';this.template.visible=false;this.scene.add(this.template);this.attachments=new THREE.Group();this.scene.add(this.attachments);this.bones=new THREE.Group();this.scene.add(this.bones);
    this.arms=new THREE.Group();this.arms.name='operator_viewhands';this.arms.visible=false;this.scene.add(this.arms);
    this.skeletonRoot=new THREE.Group();this.scene.add(this.skeletonRoot);
    this.gizmo=new TransformControls(this.camera,this.renderer.domElement);this.gizmo.setSize(.7);
    this.scene.add(this.gizmo.getHelper());
    this.gizmo.addEventListener('dragging-changed',e=>{this.controls.enabled=!e.value;if(!e.value)this.commit();});
    this.gizmo.addEventListener('objectChange',()=>{if(this.selected?.userData.bone)this.onSelect?.(this.selected.userData.bone,this.selected);this.onTransform?.(this.transformValues());});
    this.setTransformSnap({enabled:false,translate:.1,rotate:15,scale:.1});
    this.renderer.domElement.addEventListener('pointerdown',e=>this.down=[e.clientX,e.clientY]);
    this.renderer.domElement.addEventListener('pointerup',e=>{
      if(!this.down||Math.hypot(e.clientX-this.down[0],e.clientY-this.down[1])>4||this.gizmo.dragging)return;
      const r=element.getBoundingClientRect(),mouse=new THREE.Vector2((e.clientX-r.left)/r.width*2-1,-(e.clientY-r.top)/r.height*2+1);
      const ray=new THREE.Raycaster();ray.setFromCamera(mouse,this.camera);
      const hit=ray.intersectObjects(this.bones.children).find(x=>x.object.userData.bone);
      if(hit)this.selectBone(hit.object.userData.bone);
    });
    this.resize=new ResizeObserver(()=>{const w=element.clientWidth,h=element.clientHeight;if(w&&h){this.renderer.setSize(w,h);this.camera.aspect=w/h;this.camera.updateProjectionMatrix();}});
    this.resize.observe(element); this.lastFrame=performance.now();
    this.renderer.setAnimationLoop(time=>{const dt=Math.min(.1,Math.max(0,(time-this.lastFrame)/1000));this.lastFrame=time;this.mixer?.update(dt);this.updateAnimationPose();this.controls.update();this.renderer.render(this.scene,this.camera);if(time-(this.lastPlaybackUpdate||0)>80){this.lastPlaybackUpdate=time;this.onPlayback?.(this.playbackState());}});
    this.camera.position.set(16,-24,15);this.controls.target.set(0,0,2);
  }
  async load(project,view='view_model',frame=false) {
    const generation=(this.generation||0)+1;this.generation=generation;
    const stats=document.querySelector('#geometry-stats');
    const previousStats=stats.textContent;stats.textContent='Loading model…';
    const stage=Object.create(WeaponViewer.prototype);
    Object.assign(stage,{project,view,generation,assetCache:this.assetCache||(this.assetCache=new Map()),
      model:new THREE.Group(),template:new THREE.Group(),attachments:new THREE.Group(),bones:new THREE.Group(),skeletonRoot:new THREE.Group()});
    let installed=false;
    const appearance=project.material||{};
    const material=new THREE.MeshPhysicalMaterial({color:appearance.color||'#8793a6',metalness:appearance.metalness??.35,roughness:appearance.roughness??.42,specularIntensity:appearance.specular??.22});
    try{
    if(appearance.definition){
      try{
        const base=`/project-files/${project.id}/${appearance.definition}`;
        const definition=JSON.parse(await stage.readAsset(base,'text'));
        if(definition.format!=='replay-weapon-material-v1'||!Array.isArray(definition.images)||definition.images.length<3)throw new Error('Material definition is incomplete');
        const folder=base.slice(0,base.lastIndexOf('/')+1);
        const images=await Promise.all(definition.images.slice(0,3).map(async image=>{
          const data=new Uint8Array(await stage.readAsset(folder+image.file,'arrayBuffer'));
          if(data.length!==image.width*image.height*4)throw new Error(`${image.file} has an invalid RGBA payload`);
          return {data,width:image.width,height:image.height};
        }));
        const texture=(image,colorSpace=THREE.NoColorSpace)=>{const result=new THREE.DataTexture(image.data,image.width,image.height,THREE.RGBAFormat);result.colorSpace=colorSpace;result.flipY=true;result.needsUpdate=true;return result;};
        const colorSpecular=texture(images[0],THREE.SRGBColorSpace),normalGloss=texture(images[1]);
        const roughPixels=new Uint8Array(images[1].data.length);
        for(let i=0;i<roughPixels.length;i+=4){const roughness=255-images[1].data[i+3];roughPixels[i]=roughness;roughPixels[i+1]=roughness;roughPixels[i+2]=roughness;roughPixels[i+3]=255;}
        const roughness=texture({data:roughPixels,width:images[1].width,height:images[1].height});
        const emissive=texture(images[2],THREE.SRGBColorSpace);
        material.map=colorSpecular;material.specularIntensityMap=colorSpecular;material.specularIntensity=1;
        material.normalMap=normalGloss;material.roughnessMap=roughness;material.roughness=1;
        material.emissiveMap=emissive;material.emissive.set('#ffffff');material.color.set('#ffffff');material.needsUpdate=true;
      }catch(e){console.warn('Material preview:',e.message);}
    }
    const rig=project.rig?.[view];
    if(project.model){
      const stock=project.stock_views?.[view];
      const {object,text,sourceVertices}=await stage.loadGeometry(project.id,stock?.model||project.model,stock?.source||project.model_source);
      stage.model.add(object);
      stage.model.matrixAutoUpdate=false;
      if(rig?.transform)stage.model.matrix.copy(mat4(rig.transform));
      if(rig)stage.skinModel(object,text,rig,sourceVertices);
      object.traverse(mesh=>{if(mesh.isMesh){mesh.material=material;mesh.castShadow=true;mesh.receiveShadow=true;}});
    }
    const template=project.template_views?.[view]||project.stock_views?.[view];
    if(template?.model){
      const {object}=await stage.loadGeometry(project.id,template.model,template.source);
      const wire=new THREE.MeshBasicMaterial({color:0x4b78bf,wireframe:true,transparent:true,opacity:.38,depthTest:false,depthWrite:false});
      object.traverse(mesh=>{if(mesh.isMesh){mesh.material=wire;mesh.renderOrder=2;}});
      stage.template.add(object);stage.template.visible=!!this.showTemplate;
    }
    if(rig)stage.drawRig(rig);
    await stage.loadAttachments(project,view,generation,material);
    if(this.generation!==generation)return;
    const selection=this.project?.id===project.id&&this.view===view?this.selected?.userData:null;
    const selectedModel=this.selected===this.model&&this.project?.id===project.id&&this.view===view;
    this.resetPose();this.gizmo.detach();this.selected=null;
    for(const key of ['model','template','attachments','bones','skeletonRoot']){
      this.scene.remove(this[key]);this.clearGroup(this[key]);this[key]=stage[key];this.scene.add(this[key]);
    }
    this.skeleton?.dispose();this.skeleton=stage.skeleton;this.project=project;this.view=view;installed=true;
    this.setWireframe(this.wireframe||false);
    this.setTemplate(this.showTemplate||false);
    if(selectedModel)this.selectModel();else if(selection?.attachment)this.selectAttachment(selection.attachment);else if(selection?.bone)this.selectBone(selection.bone);
    let triangles=0;
    for(const group of [this.model,this.attachments])group.traverse(mesh=>{if(mesh.isMesh)triangles+=(mesh.geometry.index?.count||mesh.geometry.attributes.position.count)/3;});
    stats.textContent=`${triangles.toLocaleString()} triangles  ·  ${rig?.bones.length||0} bones  ·  Native units`;
    document.querySelector('#viewport-empty').hidden=triangles>0||this.showArms||!!(project.stock_reference&&rig);
    this.bones.visible=this.showBones||false;
    const bounds=new THREE.Box3().setFromObject(this.model);bounds.union(new THREE.Box3().setFromObject(this.attachments));if(this.template.visible)bounds.union(new THREE.Box3().setFromObject(this.template));
    if(!bounds.isEmpty()){this.grid.position.z=bounds.min.z-.07;this.ground.position.z=bounds.min.z-.04;}
    if(frame)this.frame();
    }catch(error){if(this.generation===generation)stats.textContent=previousStats;throw error;}
    finally{if(!installed){for(const key of ['model','template','attachments','bones','skeletonRoot'])stage.clearGroup(stage[key]);stage.skeleton?.dispose();stage.disposeMaterial(material);}}
  }
  async readAsset(url,type){
    const cache=this.assetCache||(this.assetCache=new Map()),key=type+':'+url;
    if(!cache.has(key)){
      const pending=readResource(url).then(blob=>blob[type]());
      cache.set(key,pending);pending.catch(()=>{if(cache.get(key)===pending)cache.delete(key);});
      if(cache.size>16)cache.delete(cache.keys().next().value);
    }
    return cache.get(key);
  }
  async loadGeometry(pid,model,source){
    if(!/\.glb$/i.test(source||'')){
      const text=await this.readAsset(`/project-files/${pid}/${model}`,'text');
      return {object:new OBJLoader().parse(text),text,sourceVertices:false};
    }
    const buffer=await this.readAsset(`/project-files/${pid}/${source}`,'arrayBuffer');
    const {GLTFLoader}=await import('three/addons/loaders/GLTFLoader.js');
    const loader=new GLTFLoader();
    loader.register(()=>({name:'ReplayGeometryMaterial',loadMaterial:()=>Promise.resolve(new THREE.MeshBasicMaterial())}));
    const result=await loader.parseAsync(buffer,'');result.scene.updateMatrixWorld(true);
    const object=new THREE.Group(),names=new Set();
    result.scene.traverse(mesh=>{
      if(!mesh.isMesh)return;
      const geometry=mesh.geometry.clone();geometry.applyMatrix4(mesh.matrixWorld);
      if(mesh.matrixWorld.determinant()<0){
        if(!geometry.index)geometry.setIndex(Array.from({length:geometry.attributes.position.count},(_,i)=>i));
        const indices=geometry.index;for(let i=0;i<indices.count;i+=3){const b=indices.getX(i+1);indices.setX(i+1,indices.getX(i+2));indices.setX(i+2,b);}
      }
      const part=new THREE.Mesh(geometry,mesh.material);
      const name=(mesh.name||'part').replace(/[^a-zA-Z0-9_]/g,'_');let candidate=name,n=1;
      while(names.has(candidate))candidate=name+'_'+n++;names.add(candidate);part.name=candidate;object.add(part);
    });
    return {object,text:'',sourceVertices:true};
  }
  disposeMaterial(material){const textures=new Set(['map','normalMap','roughnessMap','metalnessMap','emissiveMap','specularIntensityMap','specularColorMap'].map(key=>material[key]).filter(Boolean));textures.forEach(texture=>texture.dispose());material.dispose();}
  clearGroup(group){const materials=new Set();group.traverse(x=>{x.geometry?.dispose();if(x.material)(Array.isArray(x.material)?x.material:[x.material]).forEach(material=>materials.add(material));});materials.forEach(material=>this.disposeMaterial(material));group.clear();}
  async setArms(value,loadHidden=false){
    this.showArms=!!value;
    if(!this.showArms&&!loadHidden){this.arms.visible=false;return;}
    if(!this.arms.children.length&&!this.armsLoading){
      this.armsLoading=this.loadArms().finally(()=>{this.armsLoading=null;});
    }
    if(this.armsLoading)await this.armsLoading;
    this.arms.visible=this.showArms;
    this.arms.updateMatrixWorld(true);
  }
  async loadArms(){
      const response=await fetch('/library-files/viewhands.glb');
      if(!response.ok)throw new Error('The local operator-arms preview has not been prepared yet.');
      const {GLTFLoader}=await import('three/addons/loaders/GLTFLoader.js');
      const result=await new GLTFLoader().parseAsync(await response.arrayBuffer(),'');
      this.arms.add(result.scene);
      this.armBones=[];this.armSkeletons=[];
      result.scene.traverse(object=>{
        if(object.isBone)this.armBones.push(object);
        if(object.isSkinnedMesh&&!this.armSkeletons.includes(object.skeleton))this.armSkeletons.push(object.skeleton);
        if(object.isMesh){object.castShadow=true;object.receiveShadow=true;object.frustumCulled=false;}
      });
      this.armSkeletons.forEach(skeleton=>skeleton.pose());
  }
  async loadAttachments(project,view,generation,material){
    for(const asset of project.owned_assets||[]){
      const geometry=asset.pool===42&&asset.geometry;if(!geometry)continue;
      const {object}=await this.loadGeometry(project.id,geometry.model,geometry.source);if(this.generation!==generation)return;
      const group=new THREE.Group();group.name=asset.name;group.userData.attachment=asset.name;group.add(object);
      const matrix=geometry[view]?.matrix||[[1,0,0,0],[0,1,0,0],[0,0,1,0]];
      group.matrixAutoUpdate=false;group.matrix.copy(mat4(matrix));
      object.traverse(mesh=>{if(mesh.isMesh){mesh.material=material.clone();mesh.castShadow=true;mesh.receiveShadow=true;mesh.userData.attachment=asset.name;}});
      this.attachments.add(group);
    }
  }
  skinModel(object,text,rig,sourceVertices=false){
    const bones=rig.bones.map(name=>{const b=new THREE.Bone();b.name=name;return b;});
    for(let i=0;i<bones.length;i++){
      const j=i-rig.root_bones;
      if(j<0)this.skeletonRoot.add(bones[i]);
      else{bones[i].position.fromArray(rig.translations[j]);bones[i].quaternion.fromArray(rig.quats[j]).normalize();bones[i-rig.parents[j]].add(bones[i]);}
    }
    this.skeletonRoot.updateMatrixWorld(true);
    this.skeleton=new THREE.Skeleton(bones);
    const runs=[];let run=[],count=0;
    for(const line of text.split(/\r?\n/)){
      const tokens=line.trim().split(/\s+/),cmd=tokens.shift();
      if(cmd==='v')count++;
      if(cmd==='g'||cmd==='o'){if(run.length){runs.push(run);run=[];}}
      if(cmd==='f'){
        const face=tokens.filter(t=>!t.startsWith('#')).map(t=>{const n=Number(t.split('/')[0]);return n>0?n-1:count+n;});
        for(let i=1;i<face.length-1;i++)run.push(face[0],face[i],face[i+1]);
      }
    }
    if(run.length)runs.push(run);
    const meshes=[];object.traverse(m=>{if(m.isMesh)meshes.push(m);});
    let vertexOffset=0;
    const useNativeWeights=rig.native_skin_weights||!!this.project?.stock_reference;
    meshes.forEach((mesh,index)=>{
      const count=mesh.geometry.attributes.position.count,indices=[],weights=[];
      const assigned=rig.part_bones?.[mesh.name||'default'];
      const fallback=assigned||rig.bones[rig.rigid_bone];
      for(let v=0;v<count;v++){
        const influences=assigned?[{bone:assigned,weight:1}]:(useNativeWeights?rig.vertex_weights?.[sourceVertices?vertexOffset+v:runs[index]?.[v]]:null)||[{bone:fallback,weight:1}];
        const sum=influences.reduce((total,w)=>total+w.weight,0);
        for(let k=0;k<4;k++){const w=influences[k];indices.push(w?rig.bones.indexOf(w.bone):0);weights.push(w?w.weight/sum:0);}
      }
      mesh.geometry.setAttribute('skinIndex',new THREE.Uint16BufferAttribute(indices,4));
      mesh.geometry.setAttribute('skinWeight',new THREE.Float32BufferAttribute(weights,4));
      const skinned=new THREE.SkinnedMesh(mesh.geometry,mesh.material);skinned.name=mesh.name;
      skinned.bindMode=THREE.DetachedBindMode;skinned.frustumCulled=false;
      const parent=mesh.parent;parent.remove(mesh);parent.add(skinned);
      this.model.updateMatrixWorld(true);skinned.bind(this.skeleton,skinned.matrixWorld);
      vertexOffset+=count;
    });
  }
  previewBone(name,axis,amount){
    const rig=this.project?.rig?.[this.view];if(!rig||!this.skeleton)return;
    this.resetPose();const index=rig.bones.indexOf(name);if(index<0)return;
    this.skeleton.bones[index].position.setComponent(axis,this.skeleton.bones[index].position.getComponent(axis)+amount);
    this.skeletonRoot.updateMatrixWorld(true);
  }
  resetPose(){
    this.nativePreview=false;this.previewSource=null;this.action=null;
    this.mixer?.stopAllAction();this.mixer=null;
    this.skeletonRoot.matrixAutoUpdate=false;this.skeletonRoot.matrix.identity();
    this.attachments.matrixAutoUpdate=false;this.attachments.matrix.identity();
    this.arms.matrixAutoUpdate=false;this.arms.matrix.identity();
    if(this.grid)this.grid.visible=true;if(this.ground)this.ground.visible=true;
    this.armSkeletons?.forEach(skeleton=>skeleton.pose());this.arms.updateMatrixWorld(true);
    const rig=this.project?.rig?.[this.view];if(!rig||!this.skeleton)return;
    for(let i=0;i<rig.bones.length;i++){const b=this.skeleton.bones[i],j=i-rig.root_bones;if(j<0){b.position.set(0,0,0);b.quaternion.identity();}else{b.position.fromArray(rig.translations[j]);b.quaternion.fromArray(rig.quats[j]).normalize();}}this.skeletonRoot.updateMatrixWorld(true);this.updateBoneMarkers();
  }
  async playClip(index){
    const source=this.project.source_clips?.[index];if(!source)throw new Error('Choose an animation clip.');
    return this.playSource(source);
  }
  async playSource(source){
    if(source.native)await this.setArms(this.showArms,true);
    if(!this.skeleton&&!source.native)throw new Error('Import a weapon rig before playing this clip.');
    this.resetPose();const tracks=[];
    this.gizmo.detach();this.previewSource=source;this.nativePreview=!!source.native;
    // The arms export and authored model share tag_weapon as their alignment origin.
    this.nativeDriver=this.armBones?.find(b=>b.name===(this.project?.stock_reference?'j_gun':'tag_weapon'));
    this.nativeDriverBindInverse=this.nativeDriver?.matrixWorld.clone().invert();
    const mapped=new Set(),unmapped=[];
    for(const t of source.tracks){
      const weaponBones=(this.skeleton?.bones||[]).filter((b,i)=>!source.native||(i>=(this.project?.rig?.[this.view]?.root_bones||1)&&!['tag_weapon','j_gun'].includes(b.name)));
      const targets=[...weaponBones,...(this.armBones||[])].filter(bone=>bone.name===t.bone);
      if(!targets.length)unmapped.push(t.bone);else mapped.add(t.bone);
      for(const bone of targets){
        if(t.translations?.length){
          const times=t.position_times||t.translations.map((_,i)=>Math.min(i/source.fps,source.duration));
          // Replay XAnim translation channels are offsets from the model's local bind position.
          const values=source.native?t.translations.flatMap(p=>p.map((v,i)=>v+bone.position.getComponent(i))):t.translations.flat();
          tracks.push(new THREE.VectorKeyframeTrack(bone.uuid+'.position',times,values));
        }
        if(t.quaternions?.length){const times=t.rotation_times||t.quaternions.map((_,i)=>Math.min(i/source.fps,source.duration));tracks.push(new THREE.QuaternionKeyframeTrack(bone.uuid+'.quaternion',times,t.quaternions.flat()));}
      }
    }
    if(!tracks.length){this.resetPose();throw new Error('This animation has no matching bones in the preview rig.');}
    this.mixer=new THREE.AnimationMixer(this.scene);
    this.action=this.mixer.clipAction(new THREE.AnimationClip(source.name,source.duration,tracks));
    this.action.clampWhenFinished=true;this.setAnimationLoop(this.previewLoop??true);
    this.mixer.timeScale=this.previewSpeed||1;this.action.play();this.mixer.update(0);
    if(this.nativePreview&&this.nativeDriver){
      // Keep the initial weapon pose at the authoring origin while preserving clip motion.
      this.arms.updateMatrixWorld(true);
      const origin=this.armBones.find(b=>b.name==='tag_weapon')||this.nativeDriver;
      this.arms.matrix.copy(origin.matrixWorld).invert();
      if(this.grid)this.grid.visible=false;if(this.ground)this.ground.visible=false;
    }
    this.updateAnimationPose();
    this.previewMapping={matched:mapped.size,unmatched:unmapped};
    return this.previewMapping;
  }
  updateAnimationPose(){
    if(!this.nativePreview||!this.nativeDriver)return;
    this.arms.updateMatrixWorld(true);
    const motion=this.project?.stock_reference?this.nativeDriver.matrixWorld:new THREE.Matrix4().multiplyMatrices(this.nativeDriver.matrixWorld,this.nativeDriverBindInverse);
    this.skeletonRoot.matrix.copy(motion);this.skeletonRoot.updateMatrixWorld(true);
    this.attachments.matrix.copy(motion);this.attachments.updateMatrixWorld(true);
    this.updateBoneMarkers();
  }
  updateBoneMarkers(){
    if(!this.bones||!this.skeleton)return;
    const rig=this.project?.rig?.[this.view];if(!rig)return;
    const lines=[],q=new THREE.Quaternion();
    for(let i=0;i<this.skeleton.bones.length;i++){
      const bone=this.skeleton.bones[i],marker=this.bones.children[i];if(!marker?.userData.bone)continue;
      bone.getWorldPosition(marker.position);bone.getWorldQuaternion(q);marker.quaternion.copy(q);
      if(i>=rig.root_bones){const parent=this.skeleton.bones[i-rig.parents[i-rig.root_bones]];const p=parent.getWorldPosition(new THREE.Vector3());lines.push(...marker.position.toArray(),...p.toArray());}
    }
    const line=this.bones.children.find(x=>x.isLineSegments);
    if(line){line.geometry.attributes.position.array.set(lines);line.geometry.attributes.position.needsUpdate=true;line.geometry.computeBoundingSphere();}
  }
  playbackState(){return {time:this.action?.time||0,duration:this.previewSource?.duration||0,playing:!!this.action?.isRunning(),source:this.previewSource};}
  pauseAnimation(){if(!this.action)return;if(this.action.paused&&this.action.time>=this.previewSource.duration){this.action.reset().play();}else this.action.paused=!this.action.paused;}
  seekAnimation(seconds){if(!this.action)return;this.action.enabled=true;this.action.time=Math.max(0,Math.min(this.previewSource.duration,seconds));this.mixer.update(0);this.updateAnimationPose();}
  setAnimationSpeed(speed){this.previewSpeed=speed;if(this.mixer)this.mixer.timeScale=speed;}
  setAnimationLoop(loop){this.previewLoop=loop;if(this.action)this.action.setLoop(loop?THREE.LoopRepeat:THREE.LoopOnce,loop?Infinity:1);}
  drawRig(rig){
    const lines=[];
    for(let i=0;i<rig.bones.length;i++){
      const pose=rig.bind_pose[i],isIK=rig.bones[i].includes('ik_loc');
      const sphere=new THREE.Mesh(new THREE.SphereGeometry(isIK?.17:.09,12,8),new THREE.MeshBasicMaterial({color:isIK?0xe6a772:0x7698cb,depthTest:false,transparent:true,opacity:.9}));
      sphere.position.fromArray(pose.translation);sphere.quaternion.fromArray(pose.quat);sphere.userData.bone=rig.bones[i];sphere.renderOrder=10;this.bones.add(sphere);
      if(i>=rig.root_bones){const parent=rig.bind_pose[i-rig.parents[i-rig.root_bones]];lines.push(...pose.translation,...parent.translation);}
    }
    const geometry=new THREE.BufferGeometry().setAttribute('position',new THREE.Float32BufferAttribute(lines,3));
    const line=new THREE.LineSegments(geometry,new THREE.LineBasicMaterial({color:0x83a0c7,transparent:true,opacity:.5,depthTest:false}));line.renderOrder=9;this.bones.add(line);
  }
  selectBone(name){this.resetPose();this.showBones=true;this.bones.visible=true;this.selected=this.bones.children.find(x=>x.userData.bone===name);if(!this.selected)return;this.gizmo.attach(this.selected);this.onSelect?.(name,this.selected);}
  selectModel(){this.resetPose();this.gizmo.detach();this.selected=this.model;this.model.matrix.decompose(this.model.position,this.model.quaternion,this.model.scale);this.model.matrixAutoUpdate=true;this.gizmo.attach(this.model);}
  selectAttachment(name){this.gizmo.detach();const group=this.attachments.children.find(x=>x.userData.attachment===name);if(!group)return false;this.selected=group;group.matrix.decompose(group.position,group.quaternion,group.scale);group.matrixAutoUpdate=true;this.gizmo.attach(group);return true;}
  setMode(mode){this.gizmo.setMode(mode);}
  setTransformSnap({enabled,translate,rotate,scale}){this.transformSnap={enabled:!!enabled,translate:Number(translate),rotate:Number(rotate),scale:Number(scale)};this.gizmo.translationSnap=enabled&&translate>0?Number(translate):null;this.gizmo.rotationSnap=enabled&&rotate>0?THREE.MathUtils.degToRad(Number(rotate)):null;this.gizmo.scaleSnap=enabled&&scale>0?Number(scale):null;}
  transformValues(){const target=this.selected?.userData.attachment?this.selected:this.model;if(!target)return null;const position=new THREE.Vector3(),quaternion=new THREE.Quaternion(),scale=new THREE.Vector3();target.matrix.decompose(position,quaternion,scale);const rotation=new THREE.Euler().setFromQuaternion(quaternion);return {position:position.toArray(),rotation:[rotation.x,rotation.y,rotation.z].map(THREE.MathUtils.radToDeg),scale:scale.toArray()};}
  setTransformComponent(kind,axis,value){const target=this.selected?.userData.attachment?this.selected:this.model;if(!target||!Number.isFinite(value))return;target.matrix.decompose(target.position,target.quaternion,target.scale);target.matrixAutoUpdate=true;if(kind==='position')target.position.setComponent(axis,value);else if(kind==='scale')target.scale.setComponent(axis,value);else if(kind==='rotation'){const rotation=new THREE.Euler().setFromQuaternion(target.quaternion);rotation.setComponent(axis,THREE.MathUtils.degToRad(value));target.quaternion.setFromEuler(rotation);}target.updateMatrix();this.onTransform?.(this.transformValues());}
  async commit(){
    if(!this.selected)return;
    if(this.selected.userData.bone){await this.onChange({op:'bone',view:this.view,bone:this.selected.userData.bone,translation:this.selected.position.toArray(),quaternion:this.selected.quaternion.toArray()});}
    else if(this.selected.userData.attachment){
      this.selected.updateMatrix();const e=this.selected.matrix.elements;
      await this.onChange({op:'attachment_transform',asset:this.selected.userData.attachment,view:this.view,matrix:[[e[0],e[4],e[8],e[12]],[e[1],e[5],e[9],e[13]],[e[2],e[6],e[10],e[14]]]});
    }else{
      this.model.updateMatrix();const e=this.model.matrix.elements;
      await this.onChange({op:'transform',view:this.view,matrix:[[e[0],e[4],e[8],e[12]],[e[1],e[5],e[9],e[13]],[e[2],e[6],e[10],e[14]]]});
    }
  }
  frame(){
    this.scene.updateMatrixWorld(true);
    for(const group of [this.model,this.template,this.attachments,this.arms])group.traverse(object=>{if(object.isSkinnedMesh)object.computeBoundingBox();});
    const bounds=new THREE.Box3().setFromObject(this.model);
    bounds.union(new THREE.Box3().setFromObject(this.attachments));
    if(this.template.visible)bounds.union(new THREE.Box3().setFromObject(this.template));
    if(this.showArms&&this.arms.visible)bounds.union(new THREE.Box3().setFromObject(this.arms));
    if(bounds.isEmpty())return;
    const center=bounds.getCenter(new THREE.Vector3()),size=bounds.getSize(new THREE.Vector3()).length();
    this.controls.target.copy(center);this.camera.position.copy(center).add(new THREE.Vector3(.65,-1,.6).normalize().multiplyScalar(size*1.8));this.camera.near=Math.max(.01,size/1000);this.camera.far=Math.max(1000,size*100);this.camera.updateProjectionMatrix();this.controls.update();
  }
  setWireframe(value){this.wireframe=value;for(const group of [this.model,this.attachments])group.traverse(o=>{if(o.isMesh)o.material.wireframe=value;});}
  hasTemplate(){return !!this.template.children.length;}
  setTemplate(value){this.showTemplate=!!value;this.template.visible=this.showTemplate&&this.hasTemplate();return this.template.visible;}
  setBones(value){this.showBones=value;this.bones.visible=value;if(!value)this.gizmo.detach();}
  capture(){this.renderer.render(this.scene,this.camera);const a=document.createElement('a');a.download=this.project.base+'-preview.png';a.href=this.renderer.domElement.toDataURL('image/png');a.click();}
}

// Import scenes into editable OBJ groups while retaining their authored transform.
// Bone assignment is per named rigid part, appropriate for weapon mechanisms.
export async function importModel(file,resources=[]){
  const extension=file.name.split('.').pop().toLowerCase();
  const buffer=await file.arrayBuffer();let object,clips=[];
  if(extension==='obj')return {obj:new TextDecoder().decode(buffer),parts:[],clips};
  const urls=[];
  const manager=new THREE.LoadingManager();
  manager.setURLModifier(url=>{
    if(url.startsWith('data:')||url.startsWith('blob:'))return url;
    const name=decodeURIComponent(url).replace(/\\/g,'/').split('/').pop();
    const source=resources.find(f=>f.name===name);
    if(!source)throw new Error('Missing companion file: '+name+'. Select the model and its textures/buffers together.');
    const mapped=URL.createObjectURL(source);urls.push(mapped);return mapped;
  });
  try{
    if(extension==='fbx'){
      const {FBXLoader}=await import('three/addons/loaders/FBXLoader.js');object=new FBXLoader(manager).parse(buffer,'');clips=object.animations;
    }else{
      const {GLTFLoader}=await import('three/addons/loaders/GLTFLoader.js');
      const loader=new GLTFLoader(manager);
      // Geometry import uses the project's separately authored material; don't decode discarded textures.
      loader.register(()=>({name:'ReplayGeometryMaterial',loadMaterial:()=>Promise.resolve(new THREE.MeshBasicMaterial())}));
      const result=await loader.parseAsync(extension==='gltf'?new TextDecoder().decode(buffer):buffer,'');object=result.scene;clips=result.animations;
    }
    return sceneToModel(object,clips);
  }finally{urls.forEach(url=>URL.revokeObjectURL(url));}
}

export function sceneToModel(object,clips=[]){
  object.updateMatrixWorld(true);
  const rows=['# Imported by Replay Weapon Workbench'],parts=[],sourceBones=[];
  object.traverse(node=>{if(node.isBone)sourceBones.push(node);});
  const nativeNames=new Map(),used=new Set(['j_import_root']);
  for(const bone of sourceBones){
    let name=(bone.name||'bone').replace(/[^a-zA-Z0-9_:.-]/g,'_').slice(0,55),candidate=name,n=1;
    while(used.has(candidate))candidate=name+'_'+n++;
    used.add(candidate);nativeNames.set(bone,candidate);
  }
  if(sourceBones.length>127)throw new Error('Replay models support at most 128 bones including the import root.');
  const boneParents=new Map();
  for(const bone of sourceBones){let parent=bone.parent;while(parent&&!parent.isBone)parent=parent.parent;boneParents.set(bone,parent);}
  const sorted=[];
  while(sorted.length<sourceBones.length){const ready=sourceBones.filter(b=>!sorted.includes(b)&&(!boneParents.get(b)||sorted.includes(boneParents.get(b))));if(!ready.length)throw new Error('Invalid bone hierarchy');sorted.push(...ready);}
  const localPose=bone=>{
    const p=new THREE.Vector3(),q=new THREE.Quaternion(),scale=new THREE.Vector3();
    bone.matrixWorld.decompose(p,q,scale);
    const parent=boneParents.get(bone);
    if(parent){const pp=new THREE.Vector3(),pq=new THREE.Quaternion();parent.matrixWorld.decompose(pp,pq,scale);pq.invert();p.sub(pp).applyQuaternion(pq);q.premultiply(pq);}
    return {translation:p.toArray(),quat:q.normalize().toArray()};
  };
  let rig=null;
  if(sorted.length){
    const poses=sorted.map(localPose);
    rig={bones:['j_import_root',...sorted.map(b=>nativeNames.get(b))],root_bones:1,
      parents:sorted.map((b,i)=>i+1-(boneParents.get(b)?sorted.indexOf(boneParents.get(b))+1:0)),
      quats:poses.map(p=>p.quat.map(v=>Math.round(v*32767))),translations:poses.map(p=>p.translation),
      classification:Array(sorted.length+1).fill(0),bind_pose:[],rigid_bone:0,material:'',
      transform:[[1,0,0,0],[0,1,0,0],[0,0,1,0]],replace:[],part_bones:{},native_skin_weights:false};
  }
  let offset=1;
  object.traverse(mesh=>{
    if(!mesh.isMesh)return;
    if(mesh.geometry.morphAttributes.position?.length)throw new Error('Morph targets require baking into bone animation before native import.');
    const geometry=mesh.geometry;
    if(!geometry.attributes.normal)geometry.computeVertexNormals();
    const position=geometry.attributes.position,uv=geometry.attributes.uv,normals=geometry.attributes.normal;
    const indices=geometry.index,cornerCount=indices?.count??position.count;
    if(cornerCount%3)throw new Error('Imported geometry is not a triangle mesh');
    let name=(mesh.name||'part').replace(/[^a-zA-Z0-9_]/g,'_'),candidate=name,n=1;
    while(parts.includes(candidate))candidate=name+'_'+n++;
    name=candidate;parts.push(name);rows.push('g '+name);
    const v=new THREE.Vector3(),normal=new THREE.Vector3(),normalMatrix=new THREE.Matrix3().getNormalMatrix(mesh.matrixWorld);
    for(let i=0;i<position.count;i++){
      v.fromBufferAttribute(position,i).applyMatrix4(mesh.matrixWorld);rows.push(`v ${v.x} ${v.y} ${v.z}`);
      rows.push(`vt ${uv?.getX(i)||0} ${uv?.getY(i)||0}`);
      normal.fromBufferAttribute(normals,i).applyMatrix3(normalMatrix).normalize();rows.push(`vn ${normal.x} ${normal.y} ${normal.z}`);
    }
    const mirrored=mesh.matrixWorld.determinant()<0;
    for(let i=0;i<cornerCount;i+=3){const t=[0,1,2].map(k=>offset+(indices?indices.getX(i+k):i+k));if(mirrored)[t[1],t[2]]=[t[2],t[1]];rows.push('f '+t.map(x=>`${x}/${x}/${x}`).join(' '));}
    offset+=position.count;
  });
  if(offset===1)throw new Error('No mesh geometry found in this scene');
  const animations=[];
  if(rig&&clips.length){
    const mixer=new THREE.AnimationMixer(object),rest=sorted.map(b=>({p:b.position.clone(),q:b.quaternion.clone(),s:b.scale.clone()}));
    for(const clip of clips){
      if(!Number.isFinite(clip.duration)||clip.duration<=0||clip.duration>120)throw new Error('Animation duration must be 0..120 seconds');
      mixer.stopAllAction();const action=mixer.clipAction(clip);action.setLoop(THREE.LoopOnce,1);action.clampWhenFinished=true;action.play();
      const fps=30,frameCount=Math.ceil(clip.duration*fps),tracks=sorted.map(b=>({bone:nativeNames.get(b),translations:[],quaternions:[]}));
      for(let frame=0;frame<=frameCount;frame++){
        mixer.setTime(Math.min(frame/fps,clip.duration));object.updateMatrixWorld(true);
        sorted.forEach((b,i)=>{const pose=localPose(b);tracks[i].translations.push(pose.translation);tracks[i].quaternions.push(pose.quat);});
      }
      animations.push({format:'replay-animation-source-v1',name:clip.name||'Animation',fps,duration:clip.duration,tracks,notetracks:[]});
    }
    mixer.stopAllAction();sorted.forEach((b,i)=>{b.position.copy(rest[i].p);b.quaternion.copy(rest[i].q);b.scale.copy(rest[i].s);});object.updateMatrixWorld(true);
  }
  return {obj:rows.join('\n')+'\n',parts,rig,clips:animations};
}

export {THREE};
