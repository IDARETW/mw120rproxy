// Run with: node --test iw8-zonetool/tools/weapon_editor/check_keyless.mjs
// Exercises the frontend's actual URL helpers without Three.js or game assets.
import assert from 'node:assert/strict';
import {readFileSync} from 'node:fs';
import test from 'node:test';
import {createContext, runInContext} from 'node:vm';

const source=readFileSync(new URL('./web/app.js',import.meta.url),'utf8');
const lines=source.split(/\r?\n/);

function declaration(prefix){
  const matches=lines.filter(line=>line.startsWith(prefix));
  assert.equal(matches.length,1,`Expected one single-line declaration: ${prefix}`);
  return matches[0];
}

function frontend(){
  const element={innerHTML:''};
  const state={project:{id:'0123456789ab'},job:{
    id:'abcdef012345',revision:1,status:'succeeded',log:'Build complete',
    download:'/project-files/0123456789ab/builds/abcdef012345/package.zip',
    files:[{name:'custom_weapon.ff',bytes:1024}]
  }};
  // Deliberately provide no access token, location, cookie, or storage globals.
  const context=createContext({state,$:selector=>selector==='#build-result'?element:null});
  const helpers=[declaration('const escape='),
    ...lines.filter(line=>line.startsWith('function authorizedUrl(')),
    declaration('function projectUrl('),declaration('function renderJob(')];
  runInContext(helpers.join('\n'),context,{filename:'app-url-helpers.js'});
  return {context,state,element};
}

test('project files use plain local URLs without credentials',()=>{
  const {context}=frontend();
  for(const path of ['project.json','assets/fire.wav','assets/view.obj','assets/material.json']){
    assert.equal(runInContext(`projectUrl(${JSON.stringify(path)})`,context),
      `/project-files/0123456789ab/${path}`);
  }
});

test('completed builds render a download link without credentials',()=>{
  const {context,state,element}=frontend();
  runInContext('renderJob()',context);
  assert.ok(element.innerHTML.includes(`href="${state.job.download}" download`));
  assert.ok(element.innerHTML.includes('Download fastfiles'));
});

test('download URLs remain HTML-escaped',()=>{
  const {context,state,element}=frontend();
  state.job.download+='?name="local"&format=zip';
  runInContext('renderJob()',context);
  assert.ok(element.innerHTML.includes('?name=&quot;local&quot;&amp;format=zip" download'));
});

test('unfinished builds do not require a download URL',()=>{
  const {context,state,element}=frontend();
  state.job.status='queued';
  delete state.job.download;
  runInContext('renderJob()',context);
  assert.ok(element.innerHTML.includes('queued'));
  assert.ok(!element.innerHTML.includes('Download fastfiles'));
});

test('the frontend contains no retired access-token helper',()=>{
  assert.doesNotMatch(source,/\b(?:replayAccessToken|authorizedUrl)\b/);
});
