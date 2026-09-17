import {importModel} from './viewer.js?v=stock2';

self.onmessage=async({data:{file,resources}})=>{
  try{
    const result=await importModel(file,resources);
    // Blob cloning shares immutable storage instead of copying a large OBJ string into the UI.
    result.obj=new Blob([result.obj],{type:'text/plain'});
    self.postMessage({result});
  }catch(error){self.postMessage({error:error.message||String(error)});}
};
