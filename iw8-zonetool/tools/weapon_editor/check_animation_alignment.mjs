// Run with: node --test iw8-zonetool/tools/weapon_editor/check_animation_alignment.mjs
// Exercises the real native-preview animation and weapon-root alignment methods.
import assert from 'node:assert/strict';
import test from 'node:test';
import {WeaponViewer,THREE} from './web/viewer.js';

function near(actual,expected,message){
  assert.ok(Math.abs(actual-expected)<1e-5,`${message}: expected ${expected}, got ${actual}`);
}

function makeViewer(stock=false){
  const viewer=Object.create(WeaponViewer.prototype),scene=new THREE.Scene();
  const arms=new THREE.Group(),armRoot=new THREE.Bone(),tagWeapon=new THREE.Bone(),jGun=new THREE.Bone();
  armRoot.name='arm_root';tagWeapon.name='tag_weapon';jGun.name='j_gun';
  tagWeapon.position.set(5,2,1);jGun.position.set(2,-1,.5);
  armRoot.add(tagWeapon);tagWeapon.add(jGun);arms.add(armRoot);scene.add(arms);
  const weaponRoot=new THREE.Bone(),weaponChild=new THREE.Bone();weaponRoot.name='j_gun';weaponChild.name='tag_weapon';weaponRoot.add(weaponChild);
  const skeletonRoot=new THREE.Group();skeletonRoot.add(weaponRoot);scene.add(skeletonRoot);
  const rig={root_bones:1,bones:['j_gun','tag_weapon'],translations:[[0,0,0]],quats:[[0,0,0,1]]};
  const view='view_model',stockModel='stock.obj';
  Object.assign(viewer,{
    scene,arms,armBones:[armRoot,tagWeapon,jGun],
    armSkeletons:[{pose(){armRoot.position.set(0,0,0);armRoot.quaternion.identity();tagWeapon.position.set(5,2,1);tagWeapon.quaternion.identity();jGun.position.set(2,-1,.5);jGun.quaternion.identity();}}],
    skeleton:new THREE.Skeleton([weaponRoot,weaponChild]),skeletonRoot,
    attachments:new THREE.Group(),bones:new THREE.Group(),gizmo:{detach(){}},
    project:{model:stock?stockModel:'custom.obj',stock_reference:stock,
      stock_views:stock?{[view]:{model:stockModel}}:{},rig:{[view]:rig},source_clips:[],base:'alignment_test'},
    view,showArms:true,showBones:false,animationEdit:false,previewLoop:false,previewSpeed:1,
    grid:{visible:true},ground:{visible:true},
  });
  viewer.setArms=async function(){this.arms.visible=true;};
  viewer.frame=()=>{};
  return viewer;
}

function animation(){
  return {native:true,name:'driver_motion',duration:1,fps:30,tracks:[{
    bone:'tag_weapon',position_times:[0,1],translations:[[1,2,0],[3,2,0]],
    rotation_times:[0,1],quaternions:[[0,0,0,1],[0,0,Math.SQRT1_2,Math.SQRT1_2]],
  }]};
}

for(const stock of [false,true]){
  test(`${stock?'stock':'custom'} weapon follows the animated arms driver in bind space`,async()=>{
    const viewer=makeViewer(stock);
    const result=await viewer.playSource(animation());
    assert.equal(result.matched,1);
    viewer.skeletonRoot.updateMatrixWorld(true);
    near(viewer.skeletonRoot.matrix.elements[12],0,'weapon begins at its authored origin');
    near(viewer.skeletonRoot.matrix.elements[13],0,'weapon begins at its authored origin');
    near(viewer.armBones.find(b=>b.name==='tag_weapon').getWorldPosition(new THREE.Vector3()).length(),0,
      'the first animated hand-driver pose is aligned to the authored origin');

    for(const time of [0,.25,.5,.75,1]){
      viewer.seekAnimation(time);
      const drivenPose=viewer.nativeDriver.matrixWorld.clone().multiply(viewer.nativeDriverBindInverse);
      assert(drivenPose.equals(viewer.skeletonRoot.matrix),`weapon and hand driver must stay flush at ${time}s`);
      near(viewer.skeletonRoot.matrix.elements[12],2*time,`weapon follows driver translation at ${time}s`);
      near(viewer.skeletonRoot.matrix.elements[13],0,`weapon remains aligned on Y at ${time}s`);
      const expectedRotation=new THREE.Quaternion().setFromAxisAngle(new THREE.Vector3(0,0,1),Math.PI/2*time);
      const actualRotation=new THREE.Quaternion().setFromRotationMatrix(viewer.skeletonRoot.matrix);
      for(const axis of ['x','y','z','w'])near(actualRotation[axis],expectedRotation[axis],`weapon follows driver rotation at ${time}s`);
    }

    viewer.resetPose();viewer.updateAnimationPose();
    viewer.skeletonRoot.updateMatrixWorld(true);
    near(viewer.skeletonRoot.matrix.elements[12],0,'reset returns weapon to authored origin');
  });
}
