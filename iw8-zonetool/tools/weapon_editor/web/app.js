import {WeaponViewer,importModel,readResource,THREE} from './viewer.js?v=weaponmaps1';

let initialBootstrap;
try {
  const response=await fetch('/api/bootstrap');
  const body=await response.json();
  if(!response.ok)throw new Error(body.error||response.statusText);
  initialBootstrap=body;
} catch(error) {
  document.querySelector('main').textContent='Cannot connect to the local weapon editor: '+error.message+'. Check the server terminal, then reload this page.';
  throw error;
}
const preferences={
  getItem(key){try{return localStorage.getItem(key);}catch{return null;}},
  setItem(key,value){try{localStorage.setItem(key,value);}catch{}},
  removeItem(key){try{localStorage.removeItem(key);}catch{}}
};

const $=s=>document.querySelector(s), $$=s=>[...document.querySelectorAll(s)];

const escape=s=>String(s??'').replace(/[&<>"']/g,c=>({'&':'&amp;','<':'&lt;','>':'&gt;','"':'&quot;',"'":'&#39;'}[c]));

const pretty=s=>String(s).replace(/^weapon_/,'').replace(/_/g,' ').replace(/\b\w/g,c=>c.toUpperCase());

const transformSnap=JSON.parse(preferences.getItem('arsenal-transform-snap')||'{"enabled":true,"translate":0.1,"rotate":15,"scale":0.1}');
const state={boot:null,project:null,page:'overview',view:'view_model',fields:[],asset:'weapon',layout:null,selectedType:'WeaponCompleteDef',job:null,selectedBone:null,expandedSlots:new Set(),showHands:preferences.getItem('arsenal-view-hands')==='1',transformSnap};

let toastTimer,queue=Promise.resolve(),fileChoice=null;

function toast(message,error=false){$('#toast').textContent=message;$('#toast').className='show'+(error?' error':'');clearTimeout(toastTimer);toastTimer=setTimeout(()=>$('#toast').className='',error?7000:3500);}

async function api(path,body){const response=await fetch(path,body===undefined?{}:{method:'POST',headers:{'Content-Type':'application/json','X-Replay-Editor':'1'},body:JSON.stringify(body)});const value=await response.json();if(!response.ok)throw new Error(value.error||response.statusText);return value;}

function mutation(body,{render=true,reloadModel=false}={}){

  const task=async()=>{try{$('#save-state').textContent='Saving…';state.project=await api('/api/projects/'+state.project.id,{...body,revision:state.project.revision});if(render)await renderPage();else updateHeader();if(reloadModel)await viewer.load(state.project,state.view);$('#save-state').textContent='All changes saved';return state.project;}catch(e){$('#save-state').textContent='Change not saved';toast(e.message,true);throw e;}};

  queue=queue.catch(()=>{}).then(task);return queue;

}

function icon(name){const paths={overview:'M3 3h7v7H3z M14 3h7v7h-7z M3 14h7v7H3z M14 14h7v7h-7z',model:'m12 3 9 5v9l-9 5-9-5V8z M3 8l9 5 9-5 M12 13v9',rig:'M12 3v7 M4 15l8-5 8 5 M12 10v11 M9 3h6 M2 15h4 M18 15h4',attachments:'M5 3h5v6H4v6h6v6h5v-6h6v-5h-6V3z',sound:'M3 9v6h4l5 5V4L7 9z M16 8q6 4 0 8 M19 5q9 7 0 14',animation:'M5 4h14v16H5z M5 8h14 M5 16h14 M9 10l5 2-5 2z',effects:'m12 2 2 7 8 3-8 2-2 8-2-8-8-2 8-3z',data:'M4 5h16v14H4z M4 10h16 M10 5v14',layout:'M4 3h16v18H4z M8 7h8 M8 11h8 M8 15h5',builds:'m12 3 9 5v9l-9 5-9-5V8z M3 8l9 5 9-5 M12 13v9 M8 5l9 5'};return `<svg viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-linecap="round" stroke-linejoin="round" aria-hidden="true"><path d="${paths[name]||paths.data}"/></svg>`;}

const pages=[['overview','Overview'],['model','Model & materials'],['rig','Rig & hand placement'],['attachments','Attachments'],['sound','Sound'],['animation','Animation'],['effects','Effects'],['data','Weapon data'],['layout','Data layout'],['builds','Builds']];

$('#navigation').innerHTML=pages.map(([id,label],i)=>`${i===7?'<div class="nav-separator"></div>':''}<button class="nav-button" data-page="${id}" aria-label="${escape(label)}">${icon(id)}<span>${label}</span></button>`).join('');

const viewer=new WeaponViewer($('#viewport'),async action=>{
  await mutation(action,{render:false});
  if(action.op==='transform'||action.op==='attachment_transform'){
    // The gizmo already changed the live object. Saving must not clear it or detach the tool.
    viewer.project=state.project;
    return;
  }
  if(action.op==='bone'&&viewer.animationEdit){
    // Keep the paused animation and live skeleton while iterating on a tag or bone.
    viewer.project=state.project;
    return;
  }
  const bone=state.selectedBone;await viewer.load(state.project,state.view);if(bone)viewer.selectBone(bone);
},(name,object)=>{state.selectedBone=name;if(state.page==='rig')renderRigInspector(object);if(state.page==='animation')syncAnimationPoseFields();},()=>{syncTransformFields();if(state.page==='animation')syncAnimationPoseFields();});

function transformFields(){
  const values=viewer.transformValues()||{position:[0,0,0],rotation:[0,0,0],scale:[1,1,1]},field=(label,kind,values,step)=>`<div class="section-label" style="margin-top:14px">${label}</div><div class="field-row three">${['X','Y','Z'].map((axis,i)=>`<label class="field"><span>${axis}</span><input type="number" step="${step}" data-transform="${kind}:${i}" value="${values[i].toFixed(kind==='rotation'?2:3)}"></label>`).join('')}</div>`;
  return `<label class="toggle-row"><input type="checkbox" id="transform-step-enabled" ${state.transformSnap.enabled?'checked':''}> Snap gizmo to increments</label><div class="field-row three">${['Move step','Rotate °','Scale step'].map((label,i)=>`<label class="field"><span>${label}</span><input type="number" min=".001" step="${i===1?'1':'.01'}" data-transform-snap="${['translate','rotate','scale'][i]}" value="${state.transformSnap[['translate','rotate','scale'][i]]}"></label>`).join('')}</div>${field('POSITION · NATIVE UNITS','position',values.position,'.001')}${field('ROTATION · DEGREES','rotation',values.rotation,'.01')}${field('SCALE','scale',values.scale,'.001')}`;
}

function applyTransformSnap(){viewer.setTransformSnap(state.transformSnap);preferences.setItem('arsenal-transform-snap',JSON.stringify(state.transformSnap));}

function syncTransformFields(values=viewer.transformValues()){
  if(!values)return;
  $$('[data-transform]').forEach(input=>{const [kind,axis]=input.dataset.transform.split(':');const value=values[kind][Number(axis)];if(document.activeElement!==input)input.value=value.toFixed(kind==='rotation'?2:3);});
}

async function setHands(value){
  await viewer.setArms(value);
  state.showHands=!!value;
  preferences.setItem('arsenal-view-hands',state.showHands?'1':'0');
  const toggle=$('#toggle-hands'),label=state.showHands?'Hide operator arms':'Show operator arms';
  toggle.classList.toggle('active',state.showHands);
  toggle.setAttribute('aria-pressed',String(state.showHands));
  toggle.setAttribute('aria-label',label);
  toggle.title=label;
  $('#viewport-empty').hidden=state.showHands||!!state.project?.model;
}



function projectUrl(path){return `/project-files/${state.project.id}/${path}`;}

function field(path){return state.fields.find(f=>f.field===path);}

function fieldValue(path,fallback='—'){return field(path)?.value??fallback;}

function inputFor(f){if(!f)return '<span class="helper">Not present in this reference</span>';const attrs=`data-field="${escape(f.field)}" data-kind="${f.kind}" data-type="${escape(f.type||'')}" class="form-control" aria-label="${escape(f.field)}" ${f.editable?'':'disabled'}`;

  if(f.choices)return `<select ${attrs}>${Object.entries(f.choices).map(([k,v])=>`<option value="${v}" ${Number(f.value)===v?'selected':''}>${escape(k)}</option>`).join('')}</select>`;

  if(f.type==='bool')return `<select ${attrs}><option value="1" ${f.value?'selected':''}>Enabled</option><option value="0" ${!f.value?'selected':''}>Disabled</option></select>`;

  return `<input ${attrs} type="${f.kind==='scalar'?'number':'text'}" ${f.kind==='scalar'?'step="any"':''} value="${escape(f.value)}">`;

}

function control(label,path){return `<label class="field"><span>${label}</span>${inputFor(field(path))}</label>`;}

function panel(title,body,badge=''){return `<div class="panel-head"><h2>${title}</h2>${badge?`<span class="tag">${badge}</span>`:''}</div><div class="panel-body">${body}</div>`;}

function note(text,kind=''){return `<div class="notice ${kind}">${text}</div>`;}

function loadoutReferenceControl(){const value=state.project.loadout_reference||state.project.reference_name;return `<label class="field"><span>Loadout category reference</span><select data-meta="loadout_reference">${state.boot.catalog.weapons.some(w=>w.selectable&&w.name===value)?'':'<option value="">Choose a selectable weapon...</option>'}${state.boot.catalog.weapons.filter(w=>w.selectable).map(w=>`<option value="${escape(w.name)}" ${w.name===value?'selected':''}>${escape(w.base)} (${escape(pretty(w.category))})</option>`).join('')}</select></label>`;}
function property(label,value){return `<div class="property-row"><span>${label}</span><strong>${escape(value)}</strong></div>`;}

async function loadFields(asset=state.asset){state.fields=await api(`/api/projects/${state.project.id}/fields?asset=${encodeURIComponent(asset)}`);}

function updateHeader(){const p=state.project;$('#project-title').textContent=p.title;$('#project-short').textContent=p.title;$('#crumb').textContent=p.title;$('#project-id').textContent=p.id+' · Revision '+p.revision;$('#subtitle').textContent=pretty(p.category)+' · '+p.base;$$('[data-page]').forEach(b=>b.classList.toggle('active',b.dataset.page===state.page));}

async function openProject(id){state.project=await api('/api/projects/'+id);state.asset='weapon';state.selectedBone=null;state.expandedSlots.clear();preferences.setItem('arsenal-project',id);await loadFields();await renderPage();await viewer.load(state.project,state.view,true);}

async function navigate(page){if(!state.project)return;state.page=page;state.asset=page==='sound'&&state.project.reference.sfx?'sfx':'weapon';if(page==='layout'&&!state.layout)state.layout=await api('/api/layout');if(!['layout','builds'].includes(page))await loadFields();await renderPage();if(page==='rig'){viewer.setBones(true);if(state.project.rig){const name=state.project.rig[state.view].bones.find(n=>n==='tag_ik_loc_le')||state.project.rig[state.view].bones[1];if(name)viewer.selectBone(name);}}else viewer.gizmo.detach();}



async function renderPage(){

  updateHeader();const p=state.project;

  const visual=['overview','model','rig','attachments','animation'].includes(state.page);$('#visual-workspace').style.display=visual?'grid':'none';

  const tabs={overview:['Project overview','Gameplay','Dependencies'],model:['Geometry','Material'],rig:['Hand placement','Bone hierarchy'],attachments:['Attachment slots','Owned attachments'],sound:['Sound events','Packages'],animation:['Animation packages','Sources'],effects:['Effect packages','References'],data:['All fields'],layout:['Structures','Enums','Asset boundaries'],builds:['Build & validate']};

  $('#page-tabs').innerHTML=(tabs[state.page]||[]).map((x,i)=>`<button data-tab="${i}" class="${i===0?'active':''}">${x}</button>`).join('')+`<span class="tab-label">${visual?'AUTHORING VIEW':'REPLAY DATA'}</span>`;

  const content=$('#page-content');content.innerHTML='';

  if(state.page==='overview'){

    if(state.asset!=='weapon'){state.asset='weapon';await loadFields();}

    $('#inspector').innerHTML=panel('Weapon details',`<div class="section-label">IDENTITY <span class="tag green">Local</span></div><label class="field"><span>Display name</span><input data-meta="title" value="${escape(p.title)}"></label><label class="field"><span>Asset name</span><input data-meta="base" value="${escape(p.base)}"></label><label class="field"><span>Description</span><textarea data-meta="description">${escape(p.description)}</textarea></label><div class="divider"></div><div class="section-label">FIRING & AMMO</div><div class="field-row">${control('Magazine','weapon.iClipSize')}${control('Fire interval · ms','weapon.weapDef[0].iFireTime')}</div>${control('Fire mode','weapon.weapDef[0].fireType')}<div class="divider"></div>${loadoutReferenceControl()}`);

    const ms=Number(fieldValue('weapon.weapDef[0].iFireTime',0));

    content.innerHTML=`<div class="stat-strip"><div class="stat-card"><span>FIRE RATE</span><strong>${ms?Math.round(60000/ms):'—'}</strong><small>RPM</small></div><div class="stat-card"><span>MAGAZINE</span><strong>${escape(fieldValue('weapon.iClipSize'))}</strong><small>rounds</small></div><div class="stat-card"><span>ATTACHMENTS</span><strong>${p.attachment_slots.flat().length}</strong><small>available</small></div><div class="stat-card"><span>WEAPON FIELDS</span><strong>${state.fields.length.toLocaleString()}</strong><small>in this reference</small></div></div><div class="two-columns"><div class="card">${panel('Reference & assembly',property('Reference',p.reference_name)+property('Weapon category',pretty(p.category))+property('Model',p.model?.split('/').pop()||'Inherited from reference')+property('Skeleton',p.rig?`${p.rig[state.view].bones.length} bones`:'Reference skeleton')+property('Loadout ordinal',p.loadout_slot))}</div><div class="card">${panel('Continue building',`<div class="property-row"><span>Shape your silhouette</span><button class="text-button" data-page="model">Model & material →</button></div><div class="property-row"><span>Place hands and moving parts</span><button class="text-button" data-page="rig">Rig workspace →</button></div><div class="property-row"><span>Configure a complete loadout</span><button class="text-button" data-page="attachments">Attachment slots →</button></div><div class="property-row"><span>Fine-tune every native field</span><button class="text-button" data-page="data">Open inspector →</button></div>`)}</div></div>`;

  }else if(state.page==='model'){

    $('#inspector').innerHTML=panel('Model & material',`<div class="section-label">GEOMETRY</div><p class="helper">${escape(p.model?.split('/').pop()||'Reference model')}</p><button class="button full-width" data-action="import-model">Import OBJ, FBX, GLB or glTF</button><p class="helper">Select the model with its companion image files in the same picker. Embedded FBX/glTF images are extracted automatically.</p><div class="divider"></div><div class="section-label">PLACEMENT</div><div class="toolbar-row"><button class="button" data-mode="translate">Move</button><button class="button" data-mode="rotate">Rotate</button><button class="button" data-mode="scale">Scale</button></div><small class="helper">Gizmo and numeric edits save the native model transform.</small>${transformFields()}<div class="divider"></div><div class="section-label">SURFACE</div><div class="field-row"><label class="field"><span>Base color</span><input type="color" data-material="color" value="${p.material.color}"></label><label class="field"><span>Preview metalness</span><input type="number" min="0" max="1" step=".05" data-material="metalness" value="${p.material.metalness??.35}"></label></div><div class="field-row"><label class="field"><span>Native roughness</span><input type="number" min="0" max="1" step=".05" data-material="roughness" value="${p.material.roughness??.45}"></label><label class="field"><span>Native specular</span><input type="number" min="0" max="1" step=".05" data-material="specular" value="${p.material.specular??.22}"></label></div><button class="button full-width" data-action="texture">Import color texture</button><div class="toolbar-row" style="margin-top:8px"><button class="button" data-action="normal">Normal map</button><button class="button" data-action="emissive">Emissive map</button></div><p class="helper">UVs come from the imported model. Specular is packed into color alpha, roughness into normal alpha, and emissive uses the native material profile.</p>`);

    content.innerHTML=`<div class="card">${panel('Source assets',p.files.length?p.files.map(f=>property(f.name,`${(f.bytes/1024).toFixed(1)} KB`)).join(''):'<p class="helper">Imported source files stay with this project.</p>')}</div>`;

    if(p.surface_materials?.length)$('#inspector').insertAdjacentHTML('beforeend',renderSurfaceMaterials(p));
  }else if(state.page==='rig'){

    renderRigInspector();content.innerHTML=`<div class="card">${panel('Rig contract',property('Coordinate system','X forward · Y right · Z up')+property('Length unit','Native game unit (inch)')+property('Hand targets','tag_ik_loc_le / tag_ik_loc_ri')+`<div class="toolbar-row" style="margin-top:15px"><button class="button" data-action="import-rig">Import rig JSON</button>${p.rig?`<a class="button" href="${projectUrl('project.json')}" download>Download project with rig</a>`:''}</div>`)}</div>`;

    content.innerHTML+=renderParts();

  }else if(state.page==='attachments'){

    const slotLimit=12;
    content.innerHTML=note('Configure the native attachment slots. The first entry in a slot is its base option. Clone an attachment to edit its data without changing the stock asset.')+`<div class="attachment-grid">${state.boot.slots.map((slot,i)=>{const entries=p.attachment_slots[i],expanded=state.expandedSlots.has(i),visible=expanded?entries:entries.slice(0,slotLimit),hidden=entries.length-visible.length;return `<div class="attachment-card"><h3>${slot} <span class="tab-count">${entries.length}</span></h3><div class="pill-list">${visible.map((n,j)=>`<span class="pill" title="${escape(n)}">${escape(n)}<button data-remove-slot="${i}" data-index="${j}" aria-label="Remove ${escape(n)}">×</button></span>`).join('')||'<span class="helper">No attachment entries</span>'}${entries.length>slotLimit?`<button class="pill more-pill" data-expand-slot="${i}" aria-expanded="${expanded}">${expanded?'Show fewer':`+${hidden} more`}</button>`:''}</div><button class="button" data-add-slot="${i}">+ Add attachment</button></div>`;}).join('')}</div><div class="card" style="margin-top:20px">${panel('Owned attachments',ownedList(42)+`<button class="button" data-clone="42">Clone an attachment</button>`)}</div>`;
    const custom=p.owned_assets.filter(a=>a.pool===42&&a.geometry);
    $('#inspector').innerHTML=panel('Attachment geometry',`<p class="helper">Replace a cloned attachment’s XModels, then place its mesh in native weapon coordinates with the viewport gizmo.</p>${custom.map(a=>`<div class="package-item"><span>${escape(a.name)}</span><span><button class="text-button" data-select-attachment="${escape(a.name)}">Select</button> <button class="text-button" data-import-attachment="${escape(a.name)}">Replace model</button></span></div>`).join('')||'<p class="helper">Clone an attachment, then open its loadout entry to import geometry.</p>'}<div class="toolbar-row" style="margin-top:15px"><button class="button" data-attachment-mode="translate">Move</button><button class="button" data-attachment-mode="rotate">Rotate</button><button class="button" data-attachment-mode="scale">Scale</button></div><small class="helper">Placement is saved independently for view and world models.</small>`)+custom.map(a=>renderSurfaceMaterials(p,a.geometry.surface_materials||[],a.name)).join('');

  }else if(['sound','animation','effects'].includes(state.page)){

    renderPackages();

    if(state.page==='animation')await renderAnimationPreview();

  }else if(state.page==='data'){

    content.innerHTML=`<div class="card"><div class="search-row"><input id="field-search" class="search" placeholder="Search fields, types or asset references…" aria-label="Search weapon fields"><select id="asset-select" class="form-control" style="width:240px">${assetOptions()}</select><span id="field-count" class="table-count"></span></div><div id="field-table" class="table-scroll"></div></div>`;renderFieldTable();

  }else if(state.page==='layout')renderLayout();

  else if(state.page==='builds')await renderBuilds();
  if(p.stock_reference)content.insertAdjacentHTML('afterbegin',(p.stock_warnings||[]).map(w=>note(w)).join('')+note('Stock reference preview · Original geometry, tags, and skin weights; neutral preview material. Builds retain the native model and attachment assets. Import your own model to compile replacement geometry.'));

}



function renderRigInspector(object){

  const rig=state.project.rig?.[state.view];

  if(!rig){$('#inspector').innerHTML=panel('Rig & hand placement','<p class="helper">Import a native rig JSON, or import geometry to create a skeleton.</p><button class="button" data-action="import-rig">Import rig</button>');return;}

  const i=rig.bones.indexOf(state.selectedBone),pose=i>=0?rig.bind_pose[i]:null,position=object?.position.toArray()||pose?.translation;

  const rotation=pose?new THREE.Euler().setFromQuaternion(object?.quaternion||new THREE.Quaternion().fromArray(pose.quat)):null;

  const parent=i>=rig.root_bones?rig.bones[i-rig.parents[i-rig.root_bones]]:null;

  const depths=rig.bones.map((_,i)=>{let depth=0;while(i>=rig.root_bones){i-=rig.parents[i-rig.root_bones];depth++;}return depth;});

  $('#inspector').innerHTML=panel('Rig & hand placement',`<div class="section-label">HAND TARGETS</div><div class="toolbar-row"><button class="button" data-select-bone="tag_ik_loc_le">Left hand</button><button class="button" data-select-bone="tag_ik_loc_ri">Right hand</button></div>${pose?`

    <span class="skeleton-tag">${escape(state.selectedBone)}</span><div class="section-label" style="margin-top:16px">POSITION · WORLD</div>

    <div class="field-row three">${['X','Y','Z'].map((axis,i)=>`<label class="field"><span>${axis}</span><input type="number" step=".05" data-bone-coordinate="${i}" value="${position[i].toFixed(3)}" ${parent?'':'disabled'}></label>`).join('')}</div>

    <div class="section-label">ROTATION · DEGREES</div><div class="field-row three">${['X','Y','Z'].map((axis,i)=>`<label class="field"><span>${axis}</span><input type="number" step="1" data-bone-rotation="${i}" value="${THREE.MathUtils.radToDeg(rotation.toArray()[i]).toFixed(2)}" ${parent?'':'disabled'}></label>`).join('')}</div>

    <div class="toolbar-row"><button class="button" data-bone-mode="translate">Move</button><button class="button" data-bone-mode="rotate">Rotate</button></div>

    <label class="field"><span>Parent bone</span><select id="bone-parent" ${parent?'':'disabled'}>${parent?rig.bones.filter(n=>n!==state.selectedBone).map(n=>`<option ${n===parent?'selected':''} value="${escape(n)}">${escape(n)}</option>`).join(''):'<option>Root</option>'}</select></label>

    <div class="toolbar-row"><button class="button" data-action="rename-bone">Rename</button><button class="button" data-action="remove-bone" ${parent?'':'disabled'}>Remove</button></div>

    <label class="field"><span>Test movement · local X (preview only)</span><input type="range" min="-3" max="3" step=".01" value="0" id="test-bone"></label>

    <small class="helper">Bind edits save to the fastfile. Test movement resets when you change bone or project.</small>`:'<small class="helper">Choose a bone or hand target.</small>'}

    <div class="divider"></div><div class="section-label">BONE HIERARCHY <span>${rig.bones.length}</span></div><button class="button full-width" data-action="add-bone">+ Add bone or tag</button><div class="bone-list">${rig.bones.map((n,i)=>`<button style="padding-left:${8+depths[i]*10}px" class="bone-item ${n===state.selectedBone?'selected':''} ${n.includes('ik_loc')?'ik':''}" data-select-bone="${escape(n)}">${escape(n)}</button>`).join('')}</div>`);

}

function renderParts(){

  const p=state.project,rig=p.rig?.[state.view];if(!rig)return '';

  return `<div class="card">${panel('Moving parts','<p class="helper">Assign each named mesh part to a bone. New model imports use rigid parts so the viewport and Replay share the same bind space.</p>')}<table><thead><tr><th>MESH PART</th><th>BONE ASSIGNMENT</th></tr></thead><tbody>${(p.model_parts||[]).map(part=>`<tr><td>${escape(part)}</td><td><select class="form-control" data-part-bone="${escape(part)}"><option value="">Default: ${escape(rig.bones[rig.rigid_bone])}</option>${rig.bones.map(n=>`<option value="${escape(n)}" ${rig.part_bones?.[part]===n?'selected':''}>${escape(n)}</option>`).join('')}</select></td></tr>`).join('')}</tbody></table></div>`;

}

function boneDialog(operation){

  const rig=state.project.rig[state.view],name=operation==='rename'?state.selectedBone:'tag_custom';

  showModal(`<h2>${operation==='rename'?'Rename bone':'Add bone or tag'}</h2><p class="lead">Native animation and attachment packages identify bones by name.</p><label class="field"><span>Bone name</span><input id="bone-name" value="${escape(name)}" maxlength="63"></label>${operation==='add'?`<label class="field"><span>Parent</span><select id="new-bone-parent">${rig.bones.map(n=>`<option value="${escape(n)}" ${n===state.selectedBone?'selected':''}>${escape(n)}</option>`).join('')}</select></label>`:''}<button class="button primary" data-save-bone="${operation}">Save bone</button>`);

}



function ownedList(pool){const assets=state.project.owned_assets.filter(a=>a.pool===pool);return `<div class="package-list">${assets.map(a=>`<div class="package-item"><span>${escape(a.name)}${a.geometry?' <span class="tag green">Custom model</span>':''}</span>${pool===42?`<button class="text-button" data-attachment-ui="${escape(a.name)}">Loadout & model</button>`:''}<button class="text-button" data-edit-asset="${escape(a.name)}">Edit fields →</button></div>`).join('')||'<p class="helper">No owned assets yet. The reference assets remain available.</p>'}</div>`;}

function assetOptions(){return [['weapon','Weapon definition'],...(state.project.reference.sfx?[['sfx','Weapon sound package']]:[]),...state.project.owned_assets.map(a=>[a.name,a.name])].map(([id,label])=>`<option value="${escape(id)}" ${id===state.asset?'selected':''}>${escape(label)}</option>`).join('');}

let previewPackages=[],previewRequest=0;
async function renderAnimationPreview(){
  const id=state.project.id;
  $('#inspector').innerHTML=panel('Animation preview','<p class="helper">Loading package clips…</p>');
  const data=await api(`/api/projects/${id}/animation-preview`);
  if(state.project.id!==id||state.page!=='animation')return;
  previewPackages=data.packages;
  $('#inspector').innerHTML=panel('Animation preview',`
    <label class="field"><span>Animation package</span><select id="preview-package">${previewPackages.map(p=>`<option value="${escape(p.name)}">${escape(p.name)}${p.owned?' · copied':''}</option>`).join('')}</select></label>
    <label class="field"><span>Find a clip</span><input id="preview-filter" placeholder="Reload, fire, inspect…"></label>
    <label class="field"><span>Package clip</span><select id="preview-clip" style="width:100%"></select></label>
    <p id="preview-availability" class="helper"></p>
    <div class="toolbar-row"><button class="button primary" data-action="play-native-animation">Play clip</button><button class="button" id="preview-pause" data-action="pause-animation">Pause</button></div>
    <label class="field"><span>Timeline <output id="preview-time">0.00 / 0.00 s</output></span><input id="preview-seek" aria-label="Animation timeline" type="range" min="0" max="1" step="0.001" value="0"></label>
    <div class="field-row"><label class="field"><span>Playback speed</span><select id="preview-speed"><option value="0.25">0.25×</option><option value="0.5">0.5×</option><option value="1" selected>1×</option><option value="2">2×</option></select></label><label class="toggle-row"><input id="preview-loop" type="checkbox" checked> Loop</label></div>
    <p id="preview-status" class="helper">Select a package clip and press Play. The arms toggle also applies here.</p>
    <div class="divider"></div><div class="section-label">LIVE POSE ALIGNMENT</div>
    <p class="helper">Pause the clip, enable pose editing, then select a bone or tag. Gizmo and numeric changes update the paused animation immediately and save to the weapon rig.</p>
    <div class="toolbar-row"><button class="button" id="animation-edit-toggle" data-action="toggle-animation-edit" aria-pressed="false">Enable paused pose editing</button></div>
    <label class="field"><span>Bone or tag</span><select id="animation-bone"></select></label>
    <div id="animation-pose-fields"></div>
    <button class="button" data-action="stop-animation">Reset pose</button>
    ${(state.project.source_clips||[]).length?'<div class="divider"></div><h3>Imported clips</h3>':''}
    ${(state.project.source_clips||[]).map((clip,i)=>`<div class="package-item"><span>${escape(clip.name)} · ${clip.duration.toFixed(2)} s</span><span><button class="button" data-play-clip="${i}">Play</button> <button class="text-button" data-edit-animation="${i}">Edit</button></span></div>`).join('')}`);
  if(previewPackages.some(p=>p.name===state.previewPackage))$('#preview-package').value=state.previewPackage;
  fillPreviewClips();
  $('#preview-speed').value=String(viewer.previewSpeed||1);
  $('#preview-loop').checked=viewer.previewLoop??true;
  fillAnimationBoneList();updateAnimationEditControls();
}
function fillAnimationBoneList(){
  const select=$('#animation-bone'),rig=state.project.rig?.[state.view];if(!select||!rig)return;
  select.innerHTML=rig.bones.map(name=>`<option value="${escape(name)}">${escape(name)}</option>`).join('');
  const preferred=state.selectedBone&&rig.bones.includes(state.selectedBone)?state.selectedBone:rig.bones[0];if(preferred)select.value=preferred;
  if(preferred&&!viewer.selected?.userData.bone)viewer.selectBone(preferred,{preserveAnimation:true});
  renderAnimationPoseFields();
}
function renderAnimationPoseFields(){
  const root=$('#animation-pose-fields'),rig=state.project?.rig?.[state.view],name=$('#animation-bone')?.value||state.selectedBone;
  if(!root||!rig||!name){if(root)root.innerHTML='<p class="helper">No weapon rig is available for live pose editing.</p>';return;}
  const index=rig.bones.indexOf(name),editable=index>=rig.root_bones,object=viewer.selected?.userData.bone===name?viewer.selected:null,pose=object||rig.bind_pose[index];
  if(!pose){root.innerHTML='<p class="helper">Select a bone or tag to edit.</p>';return;}
  const position=object?object.position.toArray():pose.translation,quaternion=object?object.quaternion:new THREE.Quaternion().fromArray(pose.quat),rotation=new THREE.Euler().setFromQuaternion(quaternion);
  const disabled=!viewer.animationEdit||!editable;
  root.innerHTML=`<div class="section-label">POSITION · CURRENT POSE</div><div class="field-row three">${['X','Y','Z'].map((axis,i)=>`<label class="field"><span>${axis}</span><input type="number" step=".01" data-bone-coordinate="${i}" value="${position[i].toFixed(3)}" ${disabled?'disabled':''}></label>`).join('')}</div><div class="section-label">ROTATION · CURRENT POSE</div><div class="field-row three">${['X','Y','Z'].map((axis,i)=>`<label class="field"><span>${axis}</span><input type="number" step=".1" data-bone-rotation="${i}" value="${THREE.MathUtils.radToDeg(rotation.toArray()[i]).toFixed(2)}" ${disabled?'disabled':''}></label>`).join('')}</div>${editable?'':'<p class="helper">The root is fixed by the native model transform. Select a child bone or tag, or use Model placement.</p>'}`;
}
function syncAnimationPoseFields(){
  const root=$('#animation-pose-fields');if(!root||!viewer.selected?.userData.bone)return;
  const position=viewer.selected.position.toArray(),rotation=new THREE.Euler().setFromQuaternion(viewer.selected.quaternion).toArray();
  $$('[data-bone-coordinate]').forEach(input=>{if(document.activeElement!==input)input.value=position[Number(input.dataset.boneCoordinate)].toFixed(3);});
  $$('[data-bone-rotation]').forEach(input=>{if(document.activeElement!==input)input.value=THREE.MathUtils.radToDeg(rotation[Number(input.dataset.boneRotation)]).toFixed(2);});
}
function updateAnimationEditControls(){
  const button=$('#animation-edit-toggle');if(!button)return;button.classList.toggle('active',viewer.animationEdit);button.setAttribute('aria-pressed',String(viewer.animationEdit));button.textContent=viewer.animationEdit?'Pose editing enabled · pause locked':'Enable paused pose editing';
  $$('[data-bone-coordinate],[data-bone-rotation]').forEach(input=>input.disabled=!viewer.animationEdit);
}
function fillPreviewClips(){
  const select=$('#preview-clip'),previous=select.value,filter=$('#preview-filter').value.toLowerCase();
  state.previewPackage=$('#preview-package').value;
  const p=previewPackages.find(p=>p.name===state.previewPackage),unique=new Map();
  for(const event of p?.events||[]){if(!unique.has(event.asset))unique.set(event.asset,event);}
  const clips=[...unique.values()].filter(c=>c.asset.toLowerCase().includes(filter));
  select.innerHTML=clips.map(c=>`<option value="${escape(c.asset)}" ${c.available?'':'disabled'}>${escape(c.asset)}${c.available?'':' · data unavailable'}</option>`).join('');
  const preferred=clips.find(c=>c.asset===previous&&c.available)||clips.find(c=>c.available&&/_reload$/.test(c.asset))||clips.find(c=>c.available);
  if(preferred)select.value=preferred.asset;
  $('[data-action="play-native-animation"]').disabled=!preferred;
  $('#preview-availability').textContent=`${clips.filter(c=>c.available).length} playable / ${unique.size} package clips`;
}
async function playNativeAnimation(){
  const asset=$('#preview-clip').value;if(!asset)return;
  const request=++previewRequest,id=state.project.id;
  $('#preview-status').textContent='Loading native animation…';
  const source=await api(`/api/projects/${id}/animation-preview?asset=${encodeURIComponent(asset)}`);
  if(request!==previewRequest||id!==state.project.id||state.page!=='animation')return;
  if(state.view!=='view_model'){state.view='view_model';$$('[data-view]').forEach(b=>b.classList.toggle('active',b.dataset.view===state.view));await viewer.load(state.project,state.view);}
  viewer.setAnimationEdit(false);
  const mapping=await viewer.playSource(source);
  viewer.frame();
  const status=$('#preview-status');
  if(status){status.textContent=`${source.fps} FPS · ${mapping.matched} matched bones${mapping.unmatched.length?` · ${mapping.unmatched.length} absent from preview rigs`:''}. ${(source.warnings||[]).join(' ')}${source.asset?.includes('additive')?' Additive layer shown alone; the game blends it with a base pose.':''}`;status.title=mapping.unmatched.join(', ');}
}
viewer.onPlayback=({time,duration,playing,source})=>{
  if(state.page!=='animation'||!$('#preview-seek'))return;
  $('#preview-seek').max=String(duration||1);$('#preview-seek').value=String(time);
  $('#preview-time').textContent=`${time.toFixed(2)} / ${duration.toFixed(2)} s`;
  $('#preview-pause').textContent=playing?'Pause':'Resume';$('#preview-pause').disabled=!source;
};

function renderPackages(){

  const p=state.project,pool={sound:78,animation:77,effects:79}[state.page],name={sound:'Sound',animation:'Animation',effects:'Effect'}[state.page];

  const nativeFields=state.fields.filter(f=>state.page==='sound'?f.kind==='string'||f.kind==='asset':f.kind==='asset'&&f.asset_type===pool);

  const soundAliases=(p.sound_sources||[]).map(s=>`<div class="package-item"><div><strong class="mono">${escape(s.alias)}</strong><small>${escape(s.preset)}${s.event?' · '+escape(s.event):' · Unmapped'}</small><div class="sound-preview"><audio controls src="${projectUrl(s.path)}"></audio></div></div><span><button class="text-button" data-edit-sound="${escape(s.id)}">Edit</button> <button class="text-button" data-remove-sound="${escape(s.id)}">Remove</button></span></div>`).join('')||'<p class="helper">No native aliases yet. Import a PCM WAV to create one.</p>';
  $('#page-content').innerHTML=`<div class="two-columns"><div class="card">${panel(name+' packages',`<p class="helper">Clone a package to own its event mappings and references. Link the new package from the weapon’s field inspector.</p>${ownedList(pool)}<button class="button" data-clone="${pool}" style="margin-top:15px">Clone ${name.toLowerCase()} package</button>`)}</div><div class="card">${panel(state.page==='sound'?'Native audio aliases':'Source assets',state.page==='sound'?`<p class="helper">PCM WAV sources are encoded into a resident Replay SAB and their aliases are written into the custom sound bank.</p><div class="toolbar-row"><button class="button primary" data-action="audio">Import PCM WAV</button>${p.files.some(f=>/\.wav$/i.test(f.path))?'<button class="button" data-action="new-sound">Use uploaded WAV</button>':''}</div><div class="package-list">${soundAliases}</div>`:`<p class="helper">${name} packages retain the native asset graph. Individual sequences and effects are referenced by their engine asset names.</p><button class="button" data-page="data">Inspect all references</button>`)}</div></div><div class="card"><div class="search-row"><input id="field-search" class="search" aria-label="Search events" placeholder="Search ${name.toLowerCase()} events and references…"><span id="field-count" class="table-count"></span></div><div id="field-table" class="table-scroll"></div></div>`;

  state.packageFilter=true;renderFieldTable(nativeFields);

}

function renderFieldTable(source){const query=($('#field-search')?.value||'').toLowerCase();let fields=source||state.fields;

  if(!source&&['sound','animation','effects'].includes(state.page)){const pool={sound:78,animation:77,effects:79}[state.page];fields=fields.filter(f=>state.page==='sound'?f.kind==='string'||f.kind==='asset':f.kind==='asset'&&f.asset_type===pool);}

  fields=fields.filter(f=>[f.field,f.type,f.value].join(' ').toLowerCase().includes(query));

  $('#field-count').textContent=`${fields.length.toLocaleString()} fields${fields.length>200?' · Showing 200':''}`;

  $('#field-table').innerHTML=fields.length?`<table><thead><tr><th>FIELD</th><th>VALUE</th><th>OFFSET</th></tr></thead><tbody>${fields.slice(0,200).map(f=>`<tr><td><div class="field-path">${escape(f.field)}</div><div class="field-type">${escape(f.type||f.kind)}${f.asset_type?' · Asset pool '+f.asset_type:''}${f.editable?'':' · Derived / read only'}</div></td><td>${inputFor(f)}</td><td><code>0x${f.offset.toString(16).toUpperCase()}</code></td></tr>`).join('')}</tbody></table>`:'<div class="empty-state">No matching fields in this reference.</div>';

}



function renderLayout(){const l=state.layout;if(!l)return;const types=Object.values(l.types).filter(t=>['struct','union','enum'].includes(t.kind));$('#page-content').innerHTML=`<div class="stat-strip">${[['Records',l.summary.records],['Named members',l.summary.members],['Enums',l.summary.enums],['Preserved bytes',l.summary.unmapped_bytes]].map(([k,v])=>`<div class="stat-card"><span>${k.toUpperCase()}</span><strong>${v.toLocaleString()}</strong></div>`).join('')}</div><div class="layout-grid"><div class="card"><div class="search-row" style="padding:12px"><input id="type-search" class="search" placeholder="Find a structure…" aria-label="Find a structure"></div><div id="type-list" class="type-list">${types.map(t=>`<button class="type-button ${t.name===state.selectedType?'active':''}" data-type-name="${escape(t.name)}">${escape(t.name)}</button>`).join('')}</div></div><div id="type-detail"></div></div>`;renderType();}

function renderType(){const t=state.layout.types[state.selectedType];if(!t)return;$$('[data-type-name]').forEach(b=>b.classList.toggle('active',b.dataset.typeName===t.name));const rows=t.kind==='enum'?Object.entries(t.values).map(([k,v])=>`<tr><td><code>${v}</code></td><td>${escape(k)}</td><td>enum</td></tr>`).join(''):[...t.members.map(m=>({...m,row:'member'})),...(t.unmapped_ranges||[]).map((range,index)=>({...range,name:`Preserved range ${index+1}`,row:'unmapped'}))].sort((a,b)=>a.offset_bits-b.offset_bits||(a.row==='unmapped'?1:-1)).map(item=>item.row==='unmapped'?`<tr class="unmapped-row"><td><code>0x${(item.offset_bits/8).toString(16).toUpperCase()}</code></td><td>${escape(item.name)}<div class="field-type">${item.size_bits/8} bytes</div></td><td>Padding or unknown bytes · preserved verbatim</td></tr>`:`<tr><td><code>0x${(item.offset_bits/8).toString(16).toUpperCase()}</code></td><td>${escape(item.name)}<div class="field-type">${item.size_bits/8} bytes</div></td><td><button class="text-button" data-type-link="${escape(item.type)}">${escape(item.type)}</button>${item.pointer_rule?`<div class="row-description">${escape(JSON.stringify(item.pointer_rule))}</div>`:''}</td></tr>`).join('');const unmappedBytes=(t.unmapped_ranges||[]).reduce((sum,range)=>sum+range.size_bits/8,0);$('#type-detail').innerHTML=`<div class="card">${panel(escape(t.name),`<div class="layout-summary"><span class="tag">${t.size} bytes · 0x${t.size.toString(16).toUpperCase()}</span><span class="tag">${t.kind}</span>${t.asset?`<span class="tag">Asset pool ${t.asset.id}</span>`:''}${unmappedBytes?`<span class="tag">${unmappedBytes} preserved bytes</span>`:''}</div>${t.evidence?`<p class="helper">${escape(t.evidence.layout)}${t.evidence.loader_rva?' · Native loader '+t.evidence.loader_rva:''}</p>`:''}${t.unmapped_ranges?.length?`<p class="helper">Named members and every padding or unmapped range are shown below. Preserved ranges remain byte-exact without assigning unsupported semantics.</p>`:''}`)}<div class="table-scroll"><table><thead><tr><th>${t.kind==='enum'?'VALUE':'OFFSET'}</th><th>MEMBER</th><th>TYPE / RULE</th></tr></thead><tbody>${rows}</tbody></table></div></div>`;}



async function renderBuilds(){const validation=await api(`/api/projects/${state.project.id}/validate`);$('#page-content').innerHTML=`<div class="two-columns"><div class="card">${panel('Build native fastfiles',`<p class="helper">Build a snapshot of revision ${state.project.revision}. Your source project stays editable while ZoneTool compiles the package.</p><div class="divider"></div>${property('Weapon',state.project.base+'.ff')}${property('Companions','Techsets · Worldwide · English')}${property('Sound lifetime','Separate common package')}${property('Loadout ordinal',state.project.loadout_slot)}<button class="button primary" data-action="start-build" style="margin-top:20px">◈ Build this revision</button>`)}</div><div class="card">${panel('Project checks',validation.errors.length?note(validation.errors.map(escape).join('<br>'),'error'):'<span class="tag green">Project inputs valid</span>')}${validation.warnings.length?`<div class="panel-body"><ul class="validation-list">${validation.warnings.map(w=>`<li>${escape(w)}</li>`).join('')}</ul></div>`:''}</div></div><div id="build-result"></div>`;if(state.job)renderJob();}

function renderJob(){const j=state.job;if(!$('#build-result'))return;$('#build-result').innerHTML=`<div class="card"><div class="panel-body"><div class="build-heading"><h2 class="section-title">${j.status==='succeeded'?'Package built':j.status==='failed'?'Build needs attention':'Building native assets…'}</h2><span class="tag ${j.status==='succeeded'?'green':''}">${escape(j.status)}</span></div><p class="helper">Revision ${j.revision} · ${j.id}</p><pre class="build-log">${escape(j.log)}</pre>${j.download?`<a class="button primary" href="${escape(j.download)}" download>Download fastfiles</a><div class="build-files">${j.files.map(f=>`<div class="build-file"><span class="mono">${escape(f.name)}</span><span>${(f.bytes/1024).toFixed(1)} KB</span></div>`).join('')}</div>`:''}</div></div>`;}

async function startBuild(){await queue.catch(()=>{});state.job=await api(`/api/projects/${state.project.id}/build`,{});await navigate('builds');const poll=async()=>{state.job=await api('/api/jobs/'+state.job.id);renderJob();if(['building','queued'].includes(state.job.status))setTimeout(()=>poll().catch(e=>toast(e.message,true)),800);else toast(state.job.status==='succeeded'?'Native fastfile package is ready.':'Build failed. Open the log for details.',state.job.status==='failed');};await poll();}



function showModal(html){$('#modal-content').innerHTML=html;$('#modal').showModal();}

function closeModal(){$('#modal').close();}

function projectDialog(){showModal(`<h2>Your weapon projects</h2><p class="lead">Pick up where you left off.</p><div class="library-list">${state.boot.projects.map(p=>`<div class="library-item"><div><strong>${escape(p.title)}</strong><small>${escape(pretty(p.category))} · Revision ${p.revision}</small></div><button class="button" data-open-project="${p.id}">Open</button></div>`).join('')}</div>`);}

function newDialog(mode='custom'){state.stockMode=mode==='stock';const list=state.boot.catalog.weapons;showModal(`<h2>${state.stockMode?'Load stock weapon':'Start a new weapon'}</h2><p class="lead">${state.stockMode?'Open a reference project with the original model, native skeleton and tags, animation references, and weapon data. First load extracts and assembles its stock parts locally.':'Begin with a native reference. Its behavior, animation and attachment layout form your starting point.'}</p><label class="field"><span>Project name</span><input id="new-title" value="${state.stockMode?'':'Untitled weapon'}" placeholder="Use stock weapon name" maxlength="100"></label><label class="field"><span>Reference category</span><select id="new-category"><option value="">All categories</option>${Object.keys(state.boot.catalog.categories).map(c=>`<option value="${c}">${pretty(c)}</option>`).join('')}<option value="special">Equipment & special weapons</option></select></label><input id="reference-search" class="search" placeholder="Search native weapon names…" aria-label="Search reference weapons"><div id="reference-list" class="library-list"></div>`);renderReferences();}

function renderReferences(){const category=$('#new-category').value,q=$('#reference-search').value.toLowerCase();$('#reference-list').innerHTML=state.boot.catalog.weapons.filter(w=>(!state.stockMode||w.selectable)&&(!category||(category==='special'?!w.selectable:w.category===category))&&w.name.includes(q)).map(w=>`<div class="library-item"><div><strong>${escape(w.base)}</strong><small>${escape(pretty(w.category))} · ${w.field_count.toLocaleString()} fields · ${w.attachment_slots.reduce((a,b)=>a+b,0)} attachments</small></div><button class="button" data-create-reference="${escape(w.name)}">${state.stockMode?'Load stock weapon':'Use reference'}</button></div>`).join('');}

function libraryDialog(pool,slot){const list=pool===42?state.boot.catalog.attachments:state.boot.catalog.packages.filter(x=>x.pool===pool);showModal(`<h2>${slot===undefined?'Clone a native asset':'Add '+state.boot.slots[slot]+' attachment'}</h2><p class="lead">${slot===undefined?'Create an owned copy with an independent asset name.':'Choose an attachment supported by the reference assembly and skeleton.'}</p>${slot===undefined?`<label class="field"><span>New asset name</span><input id="clone-name" value="${state.project.base}/${pool===42?'attachment':pool===77?'animation':pool===78?'sound':'effect'}_01"></label>`:''}<input id="library-search" class="search" placeholder="Search ${list.length.toLocaleString()} assets…" aria-label="Search native assets"><div id="library-results" class="library-list"></div>`);

  const draw=()=>{const q=$('#library-search').value.toLowerCase(),filtered=list.filter(x=>x.name.toLowerCase().includes(q));$('#library-results').innerHTML=filtered.slice(0,100).map(a=>`<div class="library-item"><div><strong class="mono" style="font-size:11px">${escape(a.name)}</strong><small>${a.field_count?a.field_count+' fields':escape(a.type)}</small></div><button class="button" ${slot===undefined?`data-clone-source="${escape(a.name)}" data-clone-pool="${pool}"`:`data-slot-source="${escape(a.name)}" data-slot="${slot}"`}>${slot===undefined?'Clone':'Add'}</button></div>`).join('')+(filtered.length>100?'<p class="helper">Search to narrow the list.</p>':'');};$('#library-search').oninput=draw;draw();

}

function baseModelReplacements(descriptor){const slots=descriptor.model_slots||{},models=descriptor.models||{};return {view_model:[...new Set((slots.view_model?.length?slots.view_model:[models.gunXModel,models.defaultViewModel]).filter(Boolean))],world_model:[...new Set((slots.world_model?.length?slots.world_model:[models.worldModel,models.defaultWorldModel]).filter(Boolean))]};}

function genericRig(){const bones=['j_gun','tag_weapon','tag_flash','tag_brass','tag_ik_loc_le','tag_ik_loc_ri','tag_mag','tag_scope','tag_silencer'];const positions=[[0,0,0],[8,0,0],[3,-.3,1],[2,.5,-1],[0,0,-1],[1,0,-2],[3,0,1],[8,0,0]];const one={bones,root_bones:1,parents:bones.slice(1).map((_,i)=>i+1),quats:bones.slice(1).map(()=>[0,0,0,32767]),translations:positions,classification:bones.map(()=>0),bind_pose:[{quat:[0,0,0,1],translation:[0,0,0],weight:2},...positions.map(t=>({quat:[0,0,0,1],translation:t,weight:2}))],rigid_bone:0,material:'',transform:[[1,0,0,0],[0,1,0,0],[0,0,1,0]],replace:[]};const descriptor=state.boot.catalog.weapons.find(w=>w.name===state.project.reference_name),replacements=baseModelReplacements(descriptor);return {format:'replay-weapon-rig-v1',view_model:{...structuredClone(one),replace:replacements.view_model},world_model:{...structuredClone(one),replace:replacements.world_model}};}

function renderSurfaceMaterials(project,materials=project.surface_materials||[],owner=''){
  const roles=[['color_texture','Base color'],['normal_texture','Normal'],['roughness_texture','Roughness'],['metallic_texture','Metallic'],['ao_texture','Ambient occlusion'],['specular_texture','Specular'],['emissive_texture','Emissive']];
  const images=project.files.filter(file=>/\.(png|jpe?g|webp|tga)$/i.test(file.name));
  const scope=owner?' data-surface-asset="'+escape(owner)+'"':'';
  const body=materials.map(material=>{
    const maps=material.maps||{};
    const selectors=roles.map(([role,label])=>'<label class=\"field\"><span>'+label+'</span><select class=\"form-control\" data-surface-key=\"'+escape(material.key)+'\"'+scope+' data-surface-map=\"'+role+'\"><option value=\"\">Unmapped</option>'+images.map(file=>'<option value=\"'+escape(file.path)+'\" '+(maps[role]===file.path?'selected':'')+'>'+escape(file.name)+'</option>').join('')+'</select></label>').join('');
    const color=material.color||'#ffffff';
    return '<div class=\"card\" style=\"margin-top:12px\"><strong>'+escape(material.name)+'</strong><p class=\"helper\">'+escape((material.parts||[]).join(', '))+'</p><div class=\"field-row\"><label class=\"field\"><span>Color tint</span><input type=\"color\" value=\"'+escape(color)+'\" data-surface-key=\"'+escape(material.key)+'\"'+scope+' data-surface-setting=\"color\"></label><label class=\"field\"><span>Metalness</span><input type=\"number\" min=\"0\" max=\"1\" step=\".05\" value=\"'+(material.metalness??0)+'\" data-surface-key=\"'+escape(material.key)+'\"'+scope+' data-surface-setting=\"metalness\"></label></div><div class=\"field-row\"><label class=\"field\"><span>Roughness</span><input type=\"number\" min=\"0\" max=\"1\" step=\".05\" value=\"'+(material.roughness??.45)+'\" data-surface-key=\"'+escape(material.key)+'\"'+scope+' data-surface-setting=\"roughness\"></label><label class=\"field\"><span>Specular</span><input type=\"number\" min=\"0\" max=\"1\" step=\".05\" value=\"'+(material.specular??.22)+'\" data-surface-key=\"'+escape(material.key)+'\"'+scope+' data-surface-setting=\"specular\"></label></div><div class=\"field-row\">'+selectors+'</div></div>';
  }).join('');
  return panel(owner?'Attachment material maps · '+escape(owner):'Imported material maps','<p class=\"helper\">Assignments are stored per source material. Choose the image for each labeled channel; embedded and companion images are available in the lists, and every assignment updates the live preview. Replay packs these channels into its native color, normal/gloss and emissive textures.</p>'+body);
}

async function upload(file){
  const limit=(/\.obj$/i.test(file.name)?512:128)*1024*1024;
  if(!file.size||file.size>limit)throw new Error(`${file.name} must be between 1 byte and ${limit/1024/1024} MiB.`);
  const pid=state.project.id;let id=null;
  for(let offset=0;offset<file.size;offset+=4*1024*1024){
    const bytes=new Uint8Array(await file.slice(offset,offset+4*1024*1024).arrayBuffer());let binary='';
    for(let i=0;i<bytes.length;i+=32768)binary+=String.fromCharCode(...bytes.subarray(i,i+32768));
    toast(`Saving ${file.name} · ${Math.round(offset/file.size*100)}%`);
    const result=await api(`/api/projects/${pid}/upload`,{name:file.name,data:btoa(binary),chunk:{id,offset,total:file.size}});
    if(result.project){state.project=result.project;return result.path;}
    id=result.id;
  }
}

function importModelOffscreen(file,resources){
  if(!/\.(glb|gltf)$/i.test(file.name))return importModel(file,resources);
  return new Promise((resolve,reject)=>{
    const worker=new Worker('/model-worker.js?v=weaponmaps1',{type:'module'});
    worker.onmessage=event=>{worker.terminate();event.data.error?reject(new Error(event.data.error)):resolve(event.data.result);};
    worker.onerror=event=>{worker.terminate();reject(new Error(event.message||'Model conversion failed.'));};
    worker.postMessage({file,resources});
  });
}

async function handleModel(file,resources=[]){
  toast('Importing geometry…');
  const result=await importModelOffscreen(file,resources);
  let source=null;const uploadedFiles=new Map();
  for(const item of resources){const saved=await upload(item);uploadedFiles.set(item.name,saved);if(item===file)source=saved;}
  for(const item of result.textureFiles||[]){
    const saved=await upload(new File([item.file],item.name,{type:'image/png'}));uploadedFiles.set(item.name,saved);
  }
  const importedMaterials=(result.materials||[]).map(material=>({...material,maps:Object.fromEntries(
    Object.entries(material.maps||{}).map(([role,name])=>[role,uploadedFiles.get(name)||state.project.files.find(file=>file.name===name)?.path||'']))}));
  const path=await upload(new File([result.obj],file.name.replace(/\.[^.]+$/,'.obj'),{type:'text/plain'}));
  let rig=structuredClone(state.project.rig||genericRig());
  if(state.project.stock_reference&&!result.rig){for(const view of ['view_model','world_model']){delete rig[view].vertex_weights;rig[view].part_bones={};rig[view].rigid_bone=0;}}
  if(result.rig){
    const descriptor=state.boot.catalog.weapons.find(w=>w.name===state.project.reference_name);
    const view=structuredClone(result.rig),world=structuredClone(result.rig);
    // Native animation packages address the reference weapon's attachment roots.
    // Keep imported geometry on those roots so hands and weapon motion share one frame.
    const viewRoot=state.project.reference_name==='iw8_knife_mp'?'tag_accessory':'j_gun';
    view.bones[0]=viewRoot;world.bones[0]='j_gun';
    view.replace=[descriptor.models.gunXModel,descriptor.models.defaultViewModel].filter(Boolean);
    world.replace=[descriptor.models.worldModel,descriptor.models.defaultWorldModel].filter(Boolean);
    rig={format:'replay-weapon-rig-v1',view_model:view,world_model:world};
  }
  const replacements=baseModelReplacements(state.boot.catalog.weapons.find(w=>w.name===state.project.reference_name));
  rig.view_model.replace=replacements.view_model;rig.world_model.replace=replacements.world_model;
  await mutation({op:'model',path,source,clips:result.clips||[],rig,materials:importedMaterials},{reloadModel:true});
  viewer.frame();
  toast(`Model imported${result.clips?.length?` with ${result.clips.length} animation clip${result.clips.length===1?'':'s'}`:''}.`);
}

async function handleAttachmentModel(assetName,file,resources=[]){
  toast('Importing attachment geometry…');
  const result=await importModelOffscreen(file,resources);let source=null;const uploadedFiles=new Map();
  for(const item of resources){const saved=await upload(item);uploadedFiles.set(item.name,saved);if(item===file)source=saved;}
  for(const item of result.textureFiles||[]){const saved=await upload(new File([item.file],item.name,{type:'image/png'}));uploadedFiles.set(item.name,saved);}
  const importedMaterials=(result.materials||[]).map(material=>({...material,maps:Object.fromEntries(
    Object.entries(material.maps||{}).map(([role,name])=>[role,uploadedFiles.get(name)||state.project.files.find(item=>item.name===name)?.path||'']))}));
  const path=await upload(new File([result.obj],file.name.replace(/\.[^.]+$/,'.obj'),{type:'text/plain'}));
  let rig;
  if(result.rig){
    const view=structuredClone(result.rig),world=structuredClone(result.rig);view.replace=[];world.replace=[];
    rig={format:'replay-weapon-rig-v1',view_model:view,world_model:world};
  }else{
    rig=genericRig();rig.view_model.replace=[];rig.world_model.replace=[];
  }
  await mutation({op:'attachment_model',asset:assetName,path,source,rig,materials:importedMaterials},{reloadModel:true});
  viewer.selectAttachment(assetName);viewer.frame();toast('Custom attachment XModels are ready for placement.');
}

function soundDialog(item=null,preferredPath=''){
  const wavs=state.project.files.filter(f=>/\.wav$/i.test(f.path));
  if(!wavs.length){toast('Import a PCM WAV source first.',true);return;}
  const value=item||{id:'',alias:`${state.project.base}/fire`,path:preferredPath||wavs.at(-1).path,event:'',preset:'weapon_player',volume:1,pitch:1,distance:25000,looping:false};
  const events=state.fields.filter(f=>f.kind==='string'&&f.field!=='sfx.name');
  showModal(`<h2>${item?'Edit':'Create'} native sound alias</h2><p class="lead">The build embeds this WAV in a resident Replay sound bank. Map it to any string event in the cloned weapon SFX layout.</p><label class="field"><span>Alias name</span><input id="sound-alias" maxlength="120" value="${escape(value.alias)}"></label><label class="field"><span>PCM WAV source</span><select id="sound-path">${wavs.map(f=>`<option value="${escape(f.path)}" ${f.path===value.path?'selected':''}>${escape(f.name)}</option>`).join('')}</select></label><label class="field"><span>SFX event</span><select id="sound-event"><option value="">Create alias without mapping</option>${events.map(f=>`<option value="${escape(f.field)}" ${f.field===value.event?'selected':''}>${escape(f.field)}</option>`).join('')}</select></label><label class="field"><span>Mix preset</span><select id="sound-preset">${[['weapon_player','Weapon · player'],['weapon_world','Weapon · world / NPC'],['mechanical','Mechanical / foley'],['ui','UI · 2D']].map(([id,label])=>`<option value="${id}" ${id===value.preset?'selected':''}>${label}</option>`).join('')}</select></label><div class="field-row three"><label class="field"><span>Volume</span><input id="sound-volume" type="number" min="0" max="4" step=".01" value="${value.volume}"></label><label class="field"><span>Pitch</span><input id="sound-pitch" type="number" min=".25" max="4" step=".01" value="${value.pitch}"></label><label class="field"><span>Distance</span><input id="sound-distance" type="number" min="0" max="100000" step="1" value="${value.distance}"></label></div><label class="toggle-row"><input id="sound-looping" type="checkbox" ${value.looping?'checked':''}> Loop sample</label><button class="button primary" data-save-sound="${escape(value.id)}">Save native alias</button>`);
}

function animationSlots(record,result=[]){
  for(const fixup of record?.fixups||[]){
    if(fixup.kind==='asset'&&Number(fixup.asset_type)===7)result.push({field:fixup.field,name:fixup.name});
    if(fixup.kind==='record')animationSlots(fixup,result);
  }
  return result;
}

function updateAnimationSlots(selected=''){
  const packageName=$('#animation-package')?.value,select=$('#animation-event');if(!select)return;
  const owned=state.project.owned_assets.find(asset=>asset.pool===77&&asset.name===packageName);
  const slots=owned?animationSlots(owned.root):[];
  select.disabled=!slots.length;
  select.innerHTML=`<option value="">${owned?'Choose a gameplay event…':'Preview only · no package mapping'}</option>`+slots.map(slot=>`<option value="${escape(slot.field)}" ${slot.field===selected?'selected':''}>${escape(slot.field.replace(/^sfx\./,''))} · ${escape(slot.name)}</option>`).join('');
}

function animationDialog(index){
  const clip=state.project.source_clips[index];
  const notes=(clip.notetracks||[]).map(n=>`${Number(n.time).toFixed(3)} ${n.name}`).join('\n');
  const packages=state.project.owned_assets.filter(asset=>asset.pool===77),binding=clip.binding||{};
  showModal(`<h2>Native animation</h2><p class="lead">${escape(clip.name)} · ${clip.tracks.length} bone tracks · ${clip.fps} FPS</p><label class="field"><span>XAnim asset name</span><input id="animation-asset" value="${escape(clip.asset)}" maxlength="120"></label><label class="toggle-row"><input id="animation-loop" type="checkbox" ${clip.loop?'checked':''}> Loop animation</label><div class="divider"></div><div class="section-label">GAMEPLAY EVENT BINDING</div>${packages.length?`<label class="field"><span>Owned animation package</span><select id="animation-package"><option value="">Preview only · do not map</option>${packages.map(asset=>`<option value="${escape(asset.name)}" ${asset.name===binding.package?'selected':''}>${escape(asset.name)}</option>`).join('')}</select></label><label class="field"><span>Native animation event</span><select id="animation-event"></select></label><label class="toggle-row"><input id="animation-link-package" type="checkbox" checked> Link this cloned package wherever its stock source is used</label><p class="helper">The event list shows the package slot and its stock XAnim. Mapping replaces that slot with this imported clip while retaining the stock value for undo or remapping.</p>`:'<div class="notice">Clone an animation package first, then edit this clip again to map it to a gameplay event. The clip can still be previewed and built as an unreferenced XAnim.</div>'}<div class="divider"></div><div class="field-row three"><label class="field"><span>Asset type</span><input id="animation-asset-type" type="number" min="0" max="255" value="${clip.asset_type??6}"></label><label class="field"><span>IK type</span><input id="animation-ik-type" type="number" min="0" max="255" value="${clip.ik_type??1}"></label><label class="field"><span>Finger pose</span><input id="animation-finger-type" type="number" min="0" max="255" value="${clip.finger_pose_type??1}"></label></div><label class="field"><span>Notetracks · one “seconds name” per line</span><textarea id="animation-notes" rows="8" placeholder="0.100 reload_start">${escape(notes)}</textarea></label><button class="button primary" data-save-animation="${index}">Save native clip</button>`);
  updateAnimationSlots(binding.field||'');
}

function formatBytes(bytes){if(bytes<1024)return `${bytes} B`;if(bytes<1024*1024)return `${(bytes/1024).toFixed(1)} KiB`;return `${(bytes/1024/1024).toFixed(1)} MiB`;}

function chooseFiles(accept,multiple,callback){
  fileChoice={accept,multiple,callback};
  openLocalFiles();
}

function chooseFile(accept,callback){chooseFiles(accept,false,callback);}

function chooseModelFiles(attachment=null){
  chooseFiles('.obj,.mtl,.fbx,.glb,.gltf,.bin,.png,.jpg,.jpeg,.webp,.tga',true,async files=>{
    const model=files.find(f=>/\.(obj|fbx|glb|gltf)$/i.test(f.name));
    if(!model)throw new Error('Select an OBJ, FBX, GLB or glTF model.');
    await (attachment?handleAttachmentModel(attachment,model,files):handleModel(model,files));
  });
}

function openLocalFiles(){
  const input=$('#asset-input');input.accept=fileChoice.accept;input.multiple=fileChoice.multiple;input.value='';
  input.onchange=()=>{const files=[...input.files];if(files.length)Promise.resolve(fileChoice.callback(fileChoice.multiple?files:files[0])).catch(e=>toast(e.message,true));input.value='';};
  closeModal();input.click();
}

document.addEventListener('click',async e=>{const b=e.target.closest('button,a');if(!b)return;try{





  if(b.dataset.page){await navigate(b.dataset.page);return;}

  if(b.dataset.openProject){closeModal();await openProject(b.dataset.openProject);return;}

  if(b.dataset.createReference){
    b.disabled=true;const stock=state.stockMode,label=b.textContent;
    try{
      toast(stock?'Loading stock model parts and native tags…':'Creating project…');
      let p=await api('/api/projects',{reference:b.dataset.createReference,title:$('#new-title').value||(stock?'Stock '+b.dataset.createReference:'Untitled weapon'),stock});
      if(stock){
        const started=Date.now();let job=p;
        while(!['succeeded','failed'].includes(job.status)){
          b.textContent=`Loading… ${Math.round((Date.now()-started)/1000)}s`;
          await new Promise(resolve=>setTimeout(resolve,2000));
          job=await api('/api/jobs/'+job.id);
        }
        if(job.status==='failed')throw new Error(job.log);
        p={id:job.project};
      }
      state.boot.projects=await api('/api/projects');closeModal();await openProject(p.id);toast('Weapon loaded');
    }finally{b.disabled=false;b.textContent=label;}
    return;
  }

  if(b.dataset.typeName){state.selectedType=b.dataset.typeName;renderType();return;}

  if(b.dataset.typeLink){let t=state.layout.types[b.dataset.typeLink];while(t&&['pointer','array'].includes(t.kind))t=state.layout.types[t.target||t.element];if(t&&['struct','enum','union'].includes(t.kind)){state.selectedType=t.name;renderType();}return;}

  if(b.dataset.view){state.view=b.dataset.view;$$('[data-view]').forEach(x=>x.classList.toggle('active',x===b));await viewer.load(state.project,state.view,true);if(state.page==='rig')renderRigInspector();return;}

  if(b.dataset.mode){viewer.selectModel();viewer.setMode(b.dataset.mode);return;}

  if(b.dataset.boneMode){viewer.setMode(b.dataset.boneMode);return;}

  if(b.dataset.selectBone){if(!state.project.rig[state.view].bones.includes(b.dataset.selectBone)){toast('This rig does not contain that hand target.',true);return;}viewer.selectBone(b.dataset.selectBone,{preserveAnimation:viewer.animationEdit});return;}

  if(b.dataset.action==='add-bone'){boneDialog('add');return;}

  if(b.dataset.action==='rename-bone'){boneDialog('rename');return;}

  if(b.dataset.action==='remove-bone'){const name=state.selectedBone;await mutation({op:'hierarchy',view:state.view,operation:'remove',bone:name},{reloadModel:true});state.selectedBone=null;renderRigInspector();return;}

  if(b.dataset.saveBone){const name=$('#bone-name').value,op=b.dataset.saveBone;await mutation({op:'hierarchy',view:state.view,operation:op,bone:op==='add'?name:state.selectedBone,name,parent:$('#new-bone-parent')?.value},{reloadModel:true});closeModal();state.selectedBone=name;viewer.selectBone(name);return;}

  if(b.dataset.addSlot!==undefined){libraryDialog(42,Number(b.dataset.addSlot));return;}

  if(b.dataset.removeSlot!==undefined){const slots=structuredClone(state.project.attachment_slots);slots[Number(b.dataset.removeSlot)].splice(Number(b.dataset.index),1);await mutation({op:'attachments',slots});return;}

  if(b.dataset.expandSlot!==undefined){const slot=Number(b.dataset.expandSlot);if(state.expandedSlots.has(slot))state.expandedSlots.delete(slot);else state.expandedSlots.add(slot);await renderPage();return;}

  if(b.dataset.slotSource){const slots=structuredClone(state.project.attachment_slots);slots[Number(b.dataset.slot)].push(b.dataset.slotSource);await mutation({op:'attachments',slots});closeModal();return;}

  if(b.dataset.attachmentUi){const a=state.project.owned_assets.find(a=>a.name===b.dataset.attachmentUi);showModal(`<h2>Attachment loadout & model</h2><p class="lead">${escape(a.name)}</p><label class="field"><span>Display name</span><input id="attachment-title" maxlength="120" value="${escape(a.ui?.title||a.name.split('/').pop())}"></label><label class="field"><span>Loadout token</span><select id="attachment-token"><option value="">Inherit reference token</option>${(state.boot.attachment_tokens||[]).map(t=>`<option value="${escape(t)}" ${a.ui?.token===t?'selected':''}>${escape(t)}</option>`).join('')}</select></label><div class="divider"></div><div class="section-label">CUSTOM XMODEL</div><p class="helper">${a.geometry?escape(a.geometry.model.split('/').pop())+' is compiled for both view and world use.':'Replace the cloned attachment model references with imported OBJ, FBX, GLB or glTF geometry.'}</p><div class="toolbar-row"><button class="button" data-import-attachment="${escape(a.name)}">${a.geometry?'Replace':'Import'} model</button>${a.geometry?`<button class="button" data-select-attachment="${escape(a.name)}">Place in viewport</button>`:''}</div><button class="button primary" data-save-attachment-ui="${escape(a.name)}">Save entry</button>`);return;}
  if(b.dataset.saveAttachmentUi){await mutation({op:'asset_ui',asset:b.dataset.saveAttachmentUi,title:$('#attachment-title').value,token:$('#attachment-token').value});closeModal();return;}

  if(b.dataset.importAttachment){const attachment=b.dataset.importAttachment;closeModal();chooseModelFiles(attachment);return;}

  if(b.dataset.selectAttachment){closeModal();if(!viewer.selectAttachment(b.dataset.selectAttachment))toast('Import attachment geometry first.',true);else toast('Attachment selected. Use Move, Rotate or Scale.');return;}

  if(b.dataset.attachmentMode){if(!viewer.selected?.userData.attachment){const first=state.project.owned_assets.find(a=>a.pool===42&&a.geometry);if(!first||!viewer.selectAttachment(first.name)){toast('Import and select attachment geometry first.',true);return;}}viewer.setMode(b.dataset.attachmentMode);return;}
  if(b.dataset.clone){libraryDialog(Number(b.dataset.clone));return;}

  if(b.dataset.cloneSource){const name=$('#clone-name').value;await mutation({op:'clone_asset',source:b.dataset.cloneSource,pool:Number(b.dataset.clonePool),name});closeModal();toast('Owned asset created. Link it from the weapon or an attachment slot.');return;}

  if(b.dataset.editAsset){state.asset=b.dataset.editAsset;state.page='data';await loadFields();await renderPage();return;}

  if(b.dataset.action==='import-model'){chooseModelFiles();return;}

  if(b.dataset.action==='import-rig'){chooseFile('.json',async f=>mutation({op:'rig',rig:JSON.parse(await f.text())},{reloadModel:true}));return;}

  if(b.dataset.action==='audio'){chooseFile('.wav,audio/wav',async f=>{const path=await upload(f);await loadFields();soundDialog(null,path);});return;}

  if(b.dataset.action==='new-sound'){soundDialog();return;}

  if(b.dataset.editSound){soundDialog(state.project.sound_sources.find(s=>s.id===b.dataset.editSound));return;}

  if(b.dataset.removeSound){await mutation({op:'sound',operation:'remove',id:b.dataset.removeSound});return;}

  if(b.dataset.saveSound!==undefined){await mutation({op:'sound',operation:b.dataset.saveSound?'edit':'add',id:b.dataset.saveSound||undefined,alias:$('#sound-alias').value,path:$('#sound-path').value,event:$('#sound-event').value,preset:$('#sound-preset').value,volume:Number($('#sound-volume').value),pitch:Number($('#sound-pitch').value),distance:Number($('#sound-distance').value),looping:$('#sound-looping').checked});closeModal();return;}

  if(['texture','normal','emissive'].includes(b.dataset.action)){chooseFile('.png,.jpg,.jpeg,.webp,.tga',async f=>{const path=await upload(f);await mutation({op:'material',material:{[{texture:'color_texture',normal:'normal_texture',emissive:'emissive_texture'}[b.dataset.action]]:path}},{reloadModel:true});});return;}

  if(b.dataset.playClip!==undefined){await viewer.playClip(Number(b.dataset.playClip));return;}

  if(b.dataset.editAnimation!==undefined){animationDialog(Number(b.dataset.editAnimation));return;}

  if(b.dataset.saveAnimation!==undefined){
    const notes=$('#animation-notes').value.split(/\r?\n/).map(x=>x.trim()).filter(Boolean).map(line=>{const match=line.match(/^(\d+(?:\.\d+)?)\s+(.+)$/);if(!match)throw new Error('Each notetrack line needs a time followed by a name.');return {time:Number(match[1]),name:match[2]};});
    await mutation({op:'animation',index:Number(b.dataset.saveAnimation),asset:$('#animation-asset').value,loop:$('#animation-loop').checked,asset_type:Number($('#animation-asset-type').value),ik_type:Number($('#animation-ik-type').value),finger_pose_type:Number($('#animation-finger-type').value),notetracks:notes,package:$('#animation-package')?.value||'',field:$('#animation-event')?.value||'',link_package:$('#animation-link-package')?.checked??false},{render:false});
    await loadFields();await renderPage();
    closeModal();return;
  }

  if(b.dataset.action==='play-native-animation'){await playNativeAnimation();return;}
  if(b.dataset.action==='pause-animation'){viewer.pauseAnimation();return;}
  if(b.dataset.action==='toggle-animation-edit'){viewer.setAnimationEdit(!viewer.animationEdit);viewer.setBones(true);updateAnimationEditControls();renderAnimationPoseFields();return;}
  if(b.dataset.action==='stop-animation'){++previewRequest;viewer.setAnimationEdit(false);viewer.resetPose();updateAnimationEditControls();renderAnimationPoseFields();return;}

  if(b.dataset.action==='start-build'){await startBuild();return;}

  if(b.dataset.tab!==undefined){const i=Number(b.dataset.tab);if(state.page==='overview'){if(i===1)await navigate('data');if(i===2){await navigate('data');$('#field-search').value='package';renderFieldTable();}}else if(state.page==='model'&&i===1){$('#inspector').scrollTop=$('#inspector').scrollHeight;}else if(state.page==='rig'&&i===1){$('.bone-list')?.scrollIntoView({block:'nearest'});}else if(state.page==='attachments'&&i===1){$('.package-list')?.scrollIntoView({block:'center'});}else if(state.page==='layout'&&i>0){if(i===1){state.selectedType='weapType_t';const name=Object.values(state.layout.types).find(t=>t.kind==='enum')?.name;state.selectedType=name;renderType();}else{showModal(`<h2>External asset boundaries</h2><p class="lead">Weapon pointers resolve these separate native asset pools.</p><div class="library-list">${Object.entries(state.layout.external_assets).map(([name,t])=>`<div class="library-item"><div><strong>${escape(name)}</strong><small>Pool ${t.id} · ${t.size} bytes · ${escape(t.name)}</small></div></div>`).join('')}</div>`);}}else if(i===1&&['sound','animation','effects'].includes(state.page))libraryDialog({sound:78,animation:77,effects:79}[state.page]);return;}

}catch(error){toast(error.message,true);}});

document.addEventListener('change',async e=>{const input=e.target;try{

  if(input.dataset.field){const f=state.fields.find(x=>x.field===input.dataset.field);let value=input.value;if(f.kind==='scalar')value=f.type==='bool'?value==='1':Number(value);await mutation({op:'field',asset:state.asset,field:f.field,value},{render:false});await loadFields();if(state.page==='overview')await renderPage();return;}

  if(input.dataset.meta){await mutation({op:'metadata',[input.dataset.meta]:input.value},{render:false});return;}

  if(input.dataset.material){const k=input.dataset.material;await mutation({op:'material',material:{[k]:k==='color'?input.value:Number(input.value)}},{render:false,reloadModel:true});return;}

  if(input.dataset.surfaceMap){await mutation({op:'surface_material',asset:input.dataset.surfaceAsset||undefined,key:input.dataset.surfaceKey,update:{maps:{[input.dataset.surfaceMap]:input.value}}},{render:false,reloadModel:true});return;}

  if(input.dataset.surfaceSetting){const key=input.dataset.surfaceSetting;await mutation({op:'surface_material',asset:input.dataset.surfaceAsset||undefined,key:input.dataset.surfaceKey,update:{[key]:key==='color'?input.value:Number(input.value)}},{render:false,reloadModel:true});return;}

  if(input.id==='transform-step-enabled'){state.transformSnap.enabled=input.checked;applyTransformSnap();return;}

  if(input.dataset.transformSnap){const value=Number(input.value);if(!Number.isFinite(value)||value<=0)throw new Error('Transform increments must be greater than zero.');state.transformSnap[input.dataset.transformSnap]=value;applyTransformSnap();return;}

  if(input.dataset.transform){const [kind,axis]=input.dataset.transform.split(':');const value=Number(input.value);if(!Number.isFinite(value))throw new Error('Transform values must be numeric.');if(viewer.selected!==viewer.model)viewer.selectModel();viewer.setTransformComponent(kind,Number(axis),value);await viewer.commit();syncTransformFields();return;}

  if(input.id==='preview-package'){fillPreviewClips();return;}
  if(input.id==='preview-speed'){viewer.setAnimationSpeed(Number(input.value));return;}
  if(input.id==='preview-loop'){viewer.setAnimationLoop(input.checked);return;}
  if(input.id==='animation-bone'){state.selectedBone=input.value;viewer.selectBone(input.value,{preserveAnimation:viewer.animationEdit});renderAnimationPoseFields();return;}
  if(input.id==='animation-package'){updateAnimationSlots();return;}

  if(input.dataset.partBone!==undefined){await mutation({op:'part_bone',view:state.view,part:input.dataset.partBone,bone:input.value},{reloadModel:true});return;}

  if(input.id==='bone-parent'){await mutation({op:'hierarchy',view:state.view,operation:'parent',bone:state.selectedBone,parent:input.value},{reloadModel:true});viewer.selectBone(state.selectedBone);return;}

  if(input.dataset.boneRotation!==undefined){if(state.page==='animation'){viewer.setBoneComponent('rotation',Number(input.dataset.boneRotation),Number(input.value));await viewer.commit();syncAnimationPoseFields();return;}const o=viewer.selected;if(!o)return;const rotation=new THREE.Euler().setFromQuaternion(o.quaternion),values=rotation.toArray();values[Number(input.dataset.boneRotation)]=THREE.MathUtils.degToRad(Number(input.value));o.quaternion.setFromEuler(new THREE.Euler(...values));await viewer.commit();return;}

  if(input.dataset.boneCoordinate!==undefined){if(state.page==='animation'){viewer.setBoneComponent('position',Number(input.dataset.boneCoordinate),Number(input.value));await viewer.commit();syncAnimationPoseFields();return;}const o=viewer.selected;if(!o)return;o.position.setComponent(Number(input.dataset.boneCoordinate),Number(input.value));await viewer.commit();return;}

  if(input.id==='asset-select'){state.asset=input.value;await loadFields();renderFieldTable();return;}


  if(input.id==='new-category')renderReferences();

}catch(error){toast(error.message,true);}});

document.addEventListener('input',e=>{if(e.target.id==='preview-filter')fillPreviewClips();if(e.target.id==='preview-seek')viewer.seekAnimation(Number(e.target.value));if(e.target.id==='test-bone')viewer.previewBone(state.selectedBone,0,Number(e.target.value));if(e.target.id==='field-search')renderFieldTable();if(e.target.id==='reference-search')renderReferences();if(e.target.id==='type-search')$$('[data-type-name]').forEach(b=>b.hidden=!b.textContent.toLowerCase().includes(e.target.value.toLowerCase()));});

$('#projects').onclick=async()=>{state.boot.projects=await api('/api/projects');projectDialog();};$('#new-project').onclick=newDialog;$('#load-stock').onclick=()=>newDialog('stock');$('#build').onclick=()=>startBuild().catch(e=>toast(e.message,true));$('#undo').onclick=()=>mutation({op:'undo'},{reloadModel:true}).catch(()=>{});$('#redo').onclick=()=>mutation({op:'redo'},{reloadModel:true}).catch(()=>{});$('#layout-shortcut').onclick=()=>navigate('layout');$('#empty-import').onclick=()=>chooseModelFiles();$('#toggle-hands').onclick=()=>setHands(!state.showHands).catch(e=>toast(e.message,true));$('#toggle-wire').onclick=e=>{viewer.setWireframe(!viewer.wireframe);e.currentTarget.classList.toggle('active',viewer.wireframe);};$('#toggle-template').onclick=async e=>{try{if(viewer.isStockModel()){toast('Import a custom weapon model before comparing the reference wireframe.');return;}if(!viewer.hasTemplate()){e.currentTarget.disabled=true;toast('Loading reference wireframe…');await mutation({op:'template'},{render:false,reloadModel:true});}const visible=viewer.setTemplate(!viewer.showTemplate);e.currentTarget.classList.toggle('active',visible);e.currentTarget.setAttribute('aria-pressed',String(visible));}finally{e.currentTarget.disabled=false;}};$('#toggle-bones').onclick=e=>{viewer.setBones(!viewer.showBones);e.currentTarget.classList.toggle('active',viewer.showBones);};$('#frame-model').onclick=()=>viewer.frame();$('#capture-model').onclick=()=>viewer.capture();

$('#help').onclick=()=>showModal('<h2>At your fingertips</h2><p class="lead">Work in native coordinates, with a live view of your model.</p>'+property('Undo','Ctrl + Z')+property('Redo','Ctrl + Shift + Z')+property('Frame model','F')+property('Move / rotate / scale','W / E / R')+property('Orbit / pan / zoom','Left drag / Right drag / Scroll')+'<p class="helper">The local workspace saves each successful edit. Builds capture a complete project revision. Game testing remains a separate step.</p>');

document.addEventListener('keydown',e=>{const editing=['INPUT','TEXTAREA','SELECT'].includes(e.target.tagName);if(!editing&&(e.ctrlKey||e.metaKey)&&e.key.toLowerCase()==='z'){e.preventDefault();mutation({op:e.shiftKey?'redo':'undo'},{reloadModel:true}).catch(()=>{});}if(editing||e.ctrlKey||e.metaKey)return;const key=e.key.toLowerCase();if(key==='f')viewer.frame();if({w:'translate',e:'rotate',r:'scale'}[key])viewer.setMode({w:'translate',e:'rotate',r:'scale'}[key]);});



try{state.boot=initialBootstrap;applyTransformSnap();$('#connection-label').textContent='Local workspace';const saved=preferences.getItem('arsenal-project');const id=state.boot.projects.find(p=>p.id===saved)?.id||state.boot.projects[0]?.id;if(id)await openProject(id);else newDialog();if(state.showHands)setHands(true).catch(e=>{state.showHands=false;preferences.removeItem('arsenal-view-hands');toast(e.message,true);});}catch(e){toast(e.message,true);$('#subtitle').textContent=e.message;console.error(e);}
