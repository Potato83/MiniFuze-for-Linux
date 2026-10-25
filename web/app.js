const token=document.querySelector('meta[name="csrf-token"]').content;
const names=['Input 1','Input 2','Main 1/2','Virtual 3/4','Virtual 5/6','Virtual 7/8'];
let data=null,state=null,mix=0,timer=null,queue=Promise.resolve(),pending=0,errorTimer,editRevision=0,replacing=false,renderedSurface=null,monoPending=false,phantomPending=false,routingPending=false,meterPreferencesPending=false;
let audioPending=false,audioDraft=false,audioOptions=null;
const $=id=>document.getElementById(id);
function routeSelected(){return !!(data?.hardware||data?.route_port||data?.route_selected)}
const referenceMarks=[0,-6,-12,-18,-24,-30,-36,-42,-60];
const referenceDrafts=new Map();
function error(message){$('error').textContent=message;$('error').hidden=false;clearTimeout(errorTimer);errorTimer=setTimeout(()=>$('error').hidden=true,6500)}
async function request(path,body){const response=await fetch(path,{method:'POST',headers:{'Content-Type':'application/json','X-CSRF-Token':token},body:JSON.stringify(body??{})});const result=await response.json();if(!response.ok)throw Error(result.error||'Не удалось выполнить команду');return result}
// Keep the existing fader elements (and keyboard/drag focus) on a matching ack.
function surfaceKey(){return JSON.stringify([data?.preset?.links,data?.preset?.virtual_visible])}
function syncMixer(){
  renderAudio();
  if(!state||JSON.stringify(state)!==JSON.stringify(data.mixer)||renderedSurface!==surfaceKey()){
    state=structuredClone(data.mixer);render();
  }else{renderStatus();renderMeters()}
}
function renderAudio(){
  const audio=data?.audio,available=audio?.available===true;
  const rates=(audio?.rates||[]).filter(Number.isInteger),quanta=(audio?.quanta||[]).filter(Number.isInteger);
  const options=JSON.stringify([rates,quanta]);
  if(options!==audioOptions){
    $('audio-rate').innerHTML='<option value="0">Автоматически</option>'+rates.map(r=>`<option value="${r}">${r.toLocaleString('ru-RU')} Гц</option>`).join('');
    $('audio-quantum').innerHTML='<option value="0">Автоматически</option>'+quanta.map(q=>`<option value="${q}">${q} сэмплов</option>`).join('');
    audioOptions=options;audioDraft=false;
  }
  if(!audioDraft&&!audioPending){$('audio-rate').value=String(audio?.requested_rate||0);$('audio-quantum').value=String(audio?.requested_quantum||0)}
  ['audio-rate','audio-quantum','audio-remember','audio-apply'].forEach(id=>$(id).disabled=!available||audioPending);
  $('audio-apply').textContent=audioPending?'Применение…':'Применить';
  $('audio-reset').disabled=audioPending;
  $('audio-current').textContent=audioPending?'Переключение режима звука…':!available?(audio?.error||'Подключи входы и выход карты'):
    audio.actual_rate?`Карта: ${audio.actual_rate.toLocaleString('ru-RU')} Гц`:'Ожидание аудиопотоков';
  const rate=Number($('audio-rate').value)||audio?.actual_rate,quantum=Number($('audio-quantum').value);
  $('audio-period').textContent=quantum&&rate?`Один буфер: ${(1000*quantum/rate).toFixed(2)} мс. Это не полная задержка записи.`:'Размер буфера выбирает звуковая служба.';
}
function applyAudio(){
  if(audioPending||!data?.audio?.available)return;
  const value={rate:Number($('audio-rate').value),quantum:Number($('audio-quantum').value),remember:!!$('audio-remember').checked};
  if(timer)sendMixer();flushReferenceDrafts();editRevision++;audioPending=true;renderAudio();
  return action('/api/audio-settings',value,'Настройки звука применены').finally(()=>{audioPending=false;audioDraft=false;renderAudio()});
}
$('audio-rate').onchange=$('audio-quantum').onchange=()=>{audioDraft=true;renderAudio()};
$('audio-apply').onclick=applyAudio;
$('audio-reset').onclick=()=>{
  if(audioPending)return;
  if(timer)sendMixer();flushReferenceDrafts();editRevision++;audioPending=true;renderAudio();
  return action('/api/audio-reset',{},'Автоматический режим восстановлен').finally(()=>{audioPending=false;audioDraft=false;renderAudio()});
};
function action(path,body,message,receive){const revision=editRevision;pending++;queue=queue.catch(()=>{}).then(async()=>{try{const result=await request(path,typeof body==='function'?await body():body);if(receive){receive(result)}else{data=result;if(revision===editRevision&&!timer){syncMixer()}else{renderStatus();renderMeters()}}if(message)$('operation-status').textContent=message}catch(e){error(e.message);try{data=await fetch('/api/state').then(r=>r.json());if(revision===editRevision&&!timer){syncMixer()}}catch(_){}}finally{pending--}});return queue}
function sendMixer(){clearTimeout(timer);const update=structuredClone(state);timer=null;action('/api/mixer',update,'Микс обновлён')}
function change(source,key,value,panChannel){if(replacing)return;editRevision++;if(source==='master'){state.mixes[mix].master_db=value}else if(key==='pans'){state.mixes[mix].strips[source].pans[panChannel]=value}else{state.mixes[mix].strips[source][key]=value;if(source<2&&data?.preset?.links?.[mix]){const peer=1-source;state.mixes[mix].strips[peer][key]=value;document.querySelectorAll(`[data-source="${peer}"][data-level]`).forEach(el=>{if(key==='level_db')el.value=value});document.querySelectorAll(`[data-mute="${peer}"]`).forEach(el=>{if(key==='mute')el.setAttribute('aria-pressed',value)});if(key==='level_db')$('gain-'+peer).textContent=(value<=-59.9?'−∞':value.toFixed(1))+' dB'}}clearTimeout(timer);timer=setTimeout(sendMixer,160)}
// Loading a preset supersedes unsent fader edits. Previously queued writes
// finish first, and controls stay locked until the replacement is acknowledged.
function replaceMixer(path,message,body={}){
  if(replacing)return;
  clearTimeout(timer);
  timer=null;
  referenceDrafts.clear();
  editRevision++;
  replacing=true;
  renderStatus();
  return action(path,body,message).finally(()=>{replacing=false;renderStatus()});
}
function surfaceAction(path,body,message){
  if(replacing)return;
  if(timer)sendMixer();
  flushReferenceDrafts();
  editRevision++;replacing=true;renderStatus();
  return action(path,body,message).finally(()=>{replacing=false;renderStatus()});
}
function meterPair(indices){return `<div class="meter-pair">${indices.map(i=>`<div class="meter"><i class="meter-fill" data-meter="${i}"></i><i class="meter-peak" data-peak="${i}"></i></div>`).join('')}</div>`}
function referenceMeters(indices,source){
  const name=source===6?'Общий выход':names[source];
  return `<div class="meter-reference-group">${meterPair(indices)}<input class="reference-slider" type="range" min="-60" max="0" step="1" value="0" data-ref-source="${source}" data-ref-mix="${mix}" aria-label="Порог ${name}" title="Перетащи отметку порога; стрелки меняют его по шкале"></div>`;
}
function referenceReadout(source){return `<button class="reference-readout" data-ref-reset="${source}" data-ref-mix="${mix}" title="Нажми, чтобы сбросить превышение порога">Порог 0 dBFS</button>`}
function referenceKey(mixIndex,source){return mixIndex+':'+source}
function nearestReference(value){return referenceMarks.reduce((best,n)=>Math.abs(value-n)<Math.abs(value-best)?n:best,0)}
function previewReference(el){
  if(replacing||!data?.peakref)return;
  const draft={mix:Number(el.dataset.refMix),source:Number(el.dataset.refSource),db:nearestReference(Number(el.value)),queued:false};
  editRevision++;referenceDrafts.set(referenceKey(draft.mix,draft.source),draft);
  el.value=String(draft.db);renderReferences();
}
function commitReference(mixIndex,source){
  const key=referenceKey(mixIndex,source),draft=referenceDrafts.get(key);
  if(!draft||draft.queued||replacing)return;
  if(timer)sendMixer();
  draft.queued=true;editRevision++;
  return action('/api/peakref',{mix:draft.mix,source:draft.source,db:draft.db},'Порог обновлён. Сохрани микс, чтобы оставить его после перезапуска.').finally(()=>{
    if(referenceDrafts.get(key)===draft)referenceDrafts.delete(key);
    renderReferences();
  });
}
function flushReferenceDrafts(){for(const draft of [...referenceDrafts.values()])commitReference(draft.mix,draft.source)}
function renderReferences(){
  const refs=data?.peakref;
  document.querySelectorAll('[data-ref-source]').forEach(el=>{
    const m=Number(el.dataset.refMix),source=Number(el.dataset.refSource),draft=referenceDrafts.get(referenceKey(m,source));
    const value=draft?.db??refs?.db?.[m]?.[source];
    el.disabled=replacing||!refs;
    el.value=String(value??0);el.setAttribute('aria-valuetext',value==null?'Нестандартный порог':value+' dBFS');
    el.classList.toggle('reference-unknown',value==null);
    el.classList.toggle('exceeded',!draft&&!!refs?.exceeded?.[m]?.[source]);
  });
  document.querySelectorAll('[data-ref-reset]').forEach(el=>{
    const m=Number(el.dataset.refMix),source=Number(el.dataset.refReset),draft=referenceDrafts.get(referenceKey(m,source));
    const value=draft?.db??refs?.db?.[m]?.[source],hit=!draft&&!!refs?.exceeded?.[m]?.[source];
    el.disabled=replacing||!refs||value==null;
    el.textContent=value==null?'Нестандартный порог':(hit?'Превышен ':'Порог ')+value+' dBFS';
    el.classList.toggle('exceeded',hit);
    el.setAttribute('aria-label','Сбросить превышение порога '+(source===6?'Общий выход':names[source]));
  });
}
function referenceInputKey(e){
  const el=e.target;if(!el.matches('[data-ref-source]')||replacing||!data?.peakref)return;
  const current=nearestReference(Number(el.value)),index=referenceMarks.indexOf(current);
  let next;
  if(e.key==='ArrowUp'||e.key==='ArrowRight')next=referenceMarks[Math.max(0,index-1)];
  else if(e.key==='ArrowDown'||e.key==='ArrowLeft')next=referenceMarks[Math.min(referenceMarks.length-1,index+1)];
  else if(e.key==='Home')next=-60;else if(e.key==='End')next=0;else return;
  e.preventDefault();el.value=String(next);previewReference(el);
  commitReference(Number(el.dataset.refMix),Number(el.dataset.refSource));
}
function scale(){return '<div class="scale">'+[0,6,12,18,24,30,42,60].map(n=>`<span data-scale="${n}">${n}</span>`).join('')+'</div>'}
function db(value){return value>0?20*Math.log10(value):-120}
function dbText(value){const n=db(value);return n<=-99?'−∞':n.toFixed(1)+' dBFS'}
function render(){if(!state)return;const current=state.mixes[mix];let html='';names.forEach((name,i)=>{if(i>=3&&data?.preset?.virtual_visible?.[mix]?.[i-3]===false)return;const strip=current.strips[i];const indices=i<2?[6+i]:[6+2+(i-2)*2,7+2+(i-2)*2];html+=`<article class="strip"><h3>${name}</h3>${i>=3&&data?.preset?.has_template?`<button class="remove-channel quiet" data-remove="${i-3}" aria-label="Скрыть ${name}" title="Скрыть и сбросить уровень, mute и панорамы">×</button>`:''}<input class="alias name-input" type="text" maxlength="256" data-input-name="${i}" aria-label="Имя ${name}" placeholder="Имя полосы" disabled><span class="kind">${i<2?'Аналоговый вход':'Звук приложения'}</span><div class="pan-controls">${strip.pans.map((p,ch)=>`<div class="pan-control"><label for="pan-${i}-${ch}">${strip.pans.length===1?'Панорама':ch===0?'Левый':'Правый'}</label><input id="pan-${i}-${ch}" type="range" min="0" max="1" step=".01" value="${p}" data-source="${i}" data-pan="${ch}" aria-label="Панорама ${name}, канал ${ch+1}"><div class="pan-endpoints"><span>L</span><span>R</span></div></div>`).join('')}</div><div class="fader-area">${scale()}${referenceMeters(indices,i)}<input class="vertical-fader" type="range" min="-60" max="0" step=".5" value="${strip.level_db}" data-source="${i}" data-level aria-label="Уровень ${name}"></div><output class="gain" id="gain-${i}">${strip.level_db<=-59.9?'−∞':strip.level_db.toFixed(1)} dB</output><span class="peak-readout" data-level-label="${indices.join(',')}">—</span><span class="clip-label" data-clip="${indices.join(',')}"></span>${referenceReadout(i)}<button class="mute" data-mute="${i}" aria-label="Заглушить ${name}" aria-pressed="${strip.mute}">Mute</button></article>`});const output=[2*mix,2*mix+1];html+=`<article class="strip master"><h3>Общий выход</h3><span class="kind">${mix===0?'Монитор и наушники':'Микс '+(mix===1?'3/4':'5/6')}</span><div class="pan-controls">Стереомикс<br>после общего фейдера</div><div class="fader-area">${scale()}${referenceMeters(output,6)}<input class="vertical-fader" type="range" min="-60" max="0" step=".5" value="${current.master_db}" data-source="master" data-level aria-label="Уровень общего выхода"></div><output class="gain" id="gain-master">${current.master_db<=-59.9?'−∞':current.master_db.toFixed(1)} dB</output><span class="peak-readout" data-level-label="${output.join(',')}">—</span><span class="clip-label" data-clip="${output.join(',')}"></span>${referenceReadout(6)}<span class="muted small">${mix===0?'Loopback Main 7/8':mix===1?'Источник Mix 3/4':'Источник Mix 5/6'}</span></article>`;$('strips').innerHTML=html;const count=3+(data?.preset?.virtual_visible?.[mix]??[true,true,true]).filter(Boolean).length;$('strips').style.gridTemplateColumns=`repeat(${count},minmax(107px,1fr)) minmax(130px,1.15fr)`;$('strips').style.minWidth=(count*107+130)+'px';renderedSurface=surfaceKey();document.querySelectorAll('[data-mix]').forEach(b=>b.setAttribute('aria-selected',Number(b.dataset.mix)===mix));$('mix-description').textContent=['На монитор, наушники и Loopback Main 7/8','Для записи из MiniFuse Mix 3/4','Для записи из MiniFuse Mix 5/6'][mix];renderStatus();renderMeters()}
function renderPreset(){
  const preset=data.preset;
  $('link-inputs').disabled=replacing||!preset?.has_template;
  $('link-inputs').setAttribute('aria-pressed',!!preset?.links?.[mix]);
  $('virtual-controls').hidden=!preset?.has_template;
  document.querySelectorAll('[data-add-virtual]').forEach(el=>{el.hidden=preset?.virtual_visible?.[mix]?.[Number(el.dataset.addVirtual)]!==false;el.disabled=replacing});
  document.querySelectorAll('[data-input-name]').forEach(el=>{
    const alias=preset?.input_names[Number(el.dataset.inputName)]||'';
    if(replacing||document.activeElement!==el)el.value=alias;
    el.title=alias;el.disabled=replacing||!preset?.has_template;
  });
  if(replacing||document.activeElement!==$('mix-name'))$('mix-name').value=preset?.mix_names[mix]||'';
  $('mix-name').disabled=replacing||!preset?.has_template;
  $('import-mfp').disabled=replacing||!$('mfp-file').files?.length;
  $('mfp-file').disabled=replacing;
  $('export-mfp').disabled=replacing||!preset?.has_template;
}
function renderMono(){
  const device=data?.device,supported=device?.ok&&device.controls?.direct_mono===true;
  $('direct-mono').disabled=!supported||monoPending;
  $('direct-mono').setAttribute('aria-pressed',!!device?.status?.switches?.direct_mono);
  $('mono-status').textContent=monoPending?'Переключение…':!device?.ok?'USB управление недоступно':
    !supported?'Для управления Mono требуется обновлённый модуль.':
    device.status.switches.direct_mono?'Моно включено':'Моно выключено';
}
function toggleMono(){
  if(monoPending||!data?.device?.ok||data.device.controls?.direct_mono!==true)return;
  const enabled=!data.device.status.switches.direct_mono;
  editRevision++;monoPending=true;renderMono();
  return action('/api/mono',{enabled},'Direct Mono обновлён').finally(()=>{monoPending=false;renderMono()});
}
$('direct-mono').onclick=toggleMono;
function renderPhantom(){
  const device=data?.device,supported=device?.ok&&device.controls?.phantom48v===true;
  $('phantom48v').disabled=!supported||phantomPending;
  $('phantom48v').setAttribute('aria-pressed',!!device?.status?.switches?.phantom48v);
  $('phantom-status').textContent=phantomPending?'Переключение…':!device?.ok?'USB управление недоступно':
    !supported?'+48V только для чтения: требуется обновлённый модуль.':'Питание обоих XLR-входов.';
}
function togglePhantom(){
  if(phantomPending||!data?.device?.ok||data.device.controls?.phantom48v!==true)return;
  const enabled=!data.device.status.switches.phantom48v;
  editRevision++;phantomPending=true;renderPhantom();
  return action('/api/phantom',{enabled},'Фантомное питание обновлено').finally(()=>{phantomPending=false;renderPhantom()});
}
$('phantom48v').onclick=togglePhantom;
const hardwareRoutes={loopback:'loopback',usb12:'loopback_to_usb12',monitor:'loopback_to_monitor'};
function renderHardwareRouting(){
  const device=data?.device,supported=device?.ok&&device.controls?.hardware_routing===true;
  Object.entries(hardwareRoutes).forEach(([route,key])=>{
    $('hw-'+route).disabled=!supported||routingPending;
    $('hw-'+route).setAttribute('aria-pressed',!!device?.status?.switches?.[key]);
  });
  $('hw-routing-status').textContent=routingPending?'Переключение…':!device?.ok?'USB управление недоступно':
    !supported?'Для управления требуется обновлённый модуль.':'Состояние считано с карты.';
}
function toggleHardwareRoute(route){
  if(!Object.hasOwn(hardwareRoutes,route)||routingPending||!data?.device?.ok||data.device.controls?.hardware_routing!==true)return;
  const enabled=!data.device.status.switches[hardwareRoutes[route]];
  editRevision++;routingPending=true;renderHardwareRouting();
  return action('/api/hardware-routing',{route,enabled},'Аппаратный маршрут обновлён').finally(()=>{routingPending=false;renderHardwareRouting()});
}
Object.keys(hardwareRoutes).forEach(route=>$('hw-'+route).onclick=()=>toggleHardwareRoute(route));
function renderHardwareMeters(){
  const h=data?.hardware_meters,active=h?.active===true;
  $('device-meter-status').textContent=active?'С карты':h?.status==='unsupported'?'Нужен новый модуль управления':
    ['error','stale'].includes(h?.status)?'Уровни временно недоступны':'Ожидание карты';
  const width=value=>Math.max(0,Math.min(100,(db(Number.isFinite(value)?value:0)+60)/60*100));
  for(let i=0;i<4;i++){
    const fill=$('device-meter-fill-'+i),peak=$('device-meter-peak-'+i),track=$('device-meter-track-'+i);
    const level=active&&Number.isFinite(h.levels?.[i])?h.levels[i]:0;
    const label=active?dbText(level):'Нет данных';
    fill.style.width=width(active?h.display_levels?.[i]:0)+'%';
    fill.classList.toggle('hot',active&&db(level)>=(h.preferences?.orange_db??-6));
    const clipped=active&&!!h.clips?.[i];
    fill.classList.toggle('clip',clipped);
    peak.style.left=width(active?h.peaks?.[i]:0)+'%';peak.hidden=!active;
    $('device-meter-label-'+i).textContent=label+(clipped?' · перегрузка':'');
    $('device-meter-label-'+i).classList.toggle('clip',clipped);
    track.setAttribute('aria-valuenow',Math.max(-60,Math.min(0,db(level))));
    track.setAttribute('aria-valuetext',label);
  }
}
const meterPreferenceFields=['meter-orange','meter-clip-reset','meter-peak-reset'];
function renderMeterPreferences(){
  const prefs=data?.meters?.preferences;
  meterPreferenceFields.forEach(id=>{$(id).disabled=meterPreferencesPending||!prefs});
  if(prefs&&!meterPreferencesPending){
    $('meter-orange').value=String(prefs.orange_db);
    $('meter-clip-reset').value=prefs.clip_reset;
    $('meter-peak-reset').value=prefs.peak_reset;
  }
}
function changeMeterPreferences(){
  if(meterPreferencesPending||!data?.meters?.preferences)return;
  const body={version:1,orange_db:Number($('meter-orange').value),clip_reset:$('meter-clip-reset').value,peak_reset:$('meter-peak-reset').value};
  editRevision++;meterPreferencesPending=true;renderMeterPreferences();
  return action('/api/meter-preferences',body,'Настройки индикаторов сохранены').finally(()=>{meterPreferencesPending=false;renderMeterPreferences()});
}
meterPreferenceFields.forEach(id=>$(id).onchange=changeMeterPreferences);
function renderStatus(){renderMono();renderPhantom();renderHardwareRouting();renderMeterPreferences();if(!data)return;$('load').disabled=!data.has_saved||replacing;$('save').disabled=replacing;$('factory').disabled=replacing;document.querySelectorAll('#strips input,#strips button').forEach(el=>el.disabled=replacing);$('device-status').textContent=data.device.ok?'MiniFuse 2 подключена':'USB управление недоступно';$('device-dot').classList.toggle('online',data.device.ok);$('saved-status').textContent=data.session?.error?'Рабочий сеанс не сохранён':data.dirty?'Есть несохранённые изменения':'Изменений нет';const switches=data.device.status?.switches??{};[1,2].forEach(i=>{$('inst'+i).disabled=!data.device.ok;$('inst'+i).setAttribute('aria-pressed',!!switches['inst'+i])});$('phantom').textContent=data.device.ok?(switches.phantom48v?'Включено':'Выключено'):'—';const selected=$('device-port').value;const ports=data.devices.map(d=>d.port);if([...$('device-port').options].map(o=>o.value).join(',')!==ports.join(',')){$('device-port').replaceChildren(...data.devices.map(d=>new Option(d.name+' · '+d.port,d.port)));if(ports.includes(selected))$('device-port').value=selected}$('route').disabled=!routeSelected()&&!data.devices.length;$('route').textContent=data.hardware?'Отключить карту от микшера':routeSelected()?'Отменить ожидание':'Подключить входы и выход';$('route-status').textContent=data.hardware?'Оба входа и мониторный выход подключены.':routeSelected()?(data.route_error?'Карта обнаружена. Повторяю подключение…':'Ожидание MiniFuse 2. Подключение восстановится автоматически.'):'Виртуальные миксы работают отдельно от карты.';renderPreset();renderReferences();if(!data.graph_alive)error('Микшер остановлен. Перезапусти панель управления.')}
function renderMeters(){if(!data)return;renderHardwareMeters();const m=data.meters;document.querySelectorAll('[data-meter]').forEach(el=>{const idx=Number(el.dataset.meter);const val=m.active?(m.display_levels??m.levels)[idx]:0;const d=db(val);el.style.height=Math.max(0,Math.min(100,(d+60)/60*100))+'%';el.classList.toggle('hot',d>=(m.preferences?.orange_db??-6));el.classList.toggle('clip',!!m.clips[idx])});document.querySelectorAll('[data-peak]').forEach(el=>{const value=m.peaks[Number(el.dataset.peak)];el.style.bottom=Math.max(0,Math.min(100,(db(value)+60)/60*100))+'%'});document.querySelectorAll('[data-level-label]').forEach(el=>{const ids=el.dataset.levelLabel.split(',').map(Number);el.textContent=m.active?dbText(Math.max(...ids.map(i=>m.levels[i]))):'Нет данных'});document.querySelectorAll('[data-clip]').forEach(el=>{el.textContent=el.dataset.clip.split(',').some(i=>m.clips[Number(i)])?'Перегрузка':''});renderReferences()}
$('strips').addEventListener('input',e=>{const el=e.target;if(el.matches('[data-ref-source]')){previewReference(el);return}if(replacing||!el.matches('input[data-source]'))return;const source=el.dataset.source==='master'?'master':Number(el.dataset.source);const value=Number(el.value);if(el.hasAttribute('data-level')){change(source,'level_db',value);$('gain-'+source).textContent=(value<=-59.9?'−∞':value.toFixed(1))+' dB'}else change(source,'pans',value,Number(el.dataset.pan))});
$('strips').addEventListener('change',e=>{const el=e.target;if(el.matches('[data-ref-source]')){commitReference(Number(el.dataset.refMix),Number(el.dataset.refSource));return}if(!el.matches('[data-input-name]')&&timer)sendMixer()});
$('strips').addEventListener('keydown',referenceInputKey);
$('strips').addEventListener('focusout',e=>{if(e.target.matches('[data-input-name]'))rename('input',Number(e.target.dataset.inputName),e.target.value)});
function rename(kind,index,name){
  if(replacing||!data?.preset?.has_template)return;
  const current=kind==='input'?data.preset.input_names[index]:data.preset.mix_names[index];
  if(name===current)return;
  if(Array.from(name).length>128){error('Имя должно содержать не более 128 символов');return}
  if(timer)sendMixer();
  editRevision++;
  return action('/api/rename',{kind,index,name},'Имя обновлено. Сохрани микс, чтобы оставить его после перезапуска.');
}
function nameKey(e){
  const el=e.target;
  if(!el.matches('[data-input-name],#mix-name'))return;
  if(e.key==='Enter'){e.preventDefault();el.blur()}
  else if(e.key==='Escape'){
    e.preventDefault();el.value=el.id==='mix-name'?data.preset.mix_names[mix]:data.preset.input_names[Number(el.dataset.inputName)];el.blur();
  }
}
$('strips').addEventListener('keydown',nameKey);
$('mix-name').onkeydown=nameKey;
$('mix-name').onblur=e=>rename('mix',mix,e.target.value);
$('strips').addEventListener('click',e=>{const ref=e.target.closest('[data-ref-reset]');if(ref){if(!replacing){editRevision++;action('/api/reset-peakref',{mix:Number(ref.dataset.refMix),source:Number(ref.dataset.refReset)},'Превышение порога сброшено')}return}const remove=e.target.closest('[data-remove]');if(remove){surfaceAction('/api/virtual',{mix,index:Number(remove.dataset.remove),visible:false},'Канал скрыт; уровень, mute и панорамы сброшены');return}const button=e.target.closest('[data-mute]');if(replacing||!button)return;const i=Number(button.dataset.mute);change(i,'mute',!state.mixes[mix].strips[i].mute);button.setAttribute('aria-pressed',state.mixes[mix].strips[i].mute);sendMixer()});
document.querySelectorAll('[data-mix]').forEach(b=>b.addEventListener('click',()=>{if(timer)sendMixer();flushReferenceDrafts();mix=Number(b.dataset.mix);render()}));
$('save').onclick=()=>{if(timer)sendMixer();flushReferenceDrafts();action('/api/save',{},'Микс сохранён')};$('load').onclick=()=>replaceMixer('/api/load','Сохранённый микс загружен');$('factory').onclick=()=>replaceMixer('/api/factory','Восстановлены заводские маршруты');$('reset-meters').onclick=()=>{editRevision++;return action('/api/reset-meters',{},'Пики сброшены')};$('route').onclick=()=>action('/api/routes',{enabled:!routeSelected(),port:data.hardware?.port||data.route_port||$('device-port').value},routeSelected()?'Карта отключена от микшера':'Карта подключена');[1,2].forEach(i=>$('inst'+i).onclick=()=>action('/api/inst',{channel:i,enabled:!data.device.status.switches['inst'+i]},'INST '+i+' обновлён'));

$('link-inputs').onclick=()=>surfaceAction('/api/link',{mix,enabled:!data?.preset?.links?.[mix]},'Связь Input 1/2 обновлена');
document.querySelectorAll('[data-add-virtual]').forEach(el=>el.onclick=()=>surfaceAction('/api/virtual',{mix,index:Number(el.dataset.addVirtual),visible:true},'Виртуальный канал добавлен'));

$('mfp-file').onchange=()=>renderStatus();
$('import-mfp').onclick=()=>{
  const file=$('mfp-file').files?.[0];
  if(!file||replacing)return;
  if(file.size>32768){error('Пресет больше 32 КиБ');return}
  replaceMixer('/api/import-mfp','Пресет .mfp применён. Сохрани микс, чтобы оставить его после перезапуска.',async()=>({text:await file.text()}));
};
$('export-mfp').onclick=()=>{
  if(replacing||!data?.preset?.has_template)return;
  if(timer)sendMixer();
  flushReferenceDrafts();
  action('/api/export-mfp',{},'Пресет .mfp подготовлен к скачиванию',result=>{
    const url=URL.createObjectURL(new Blob([result.text],{type:'application/octet-stream'}));
    const link=document.createElement('a');link.href=url;link.download='MiniFuse2.mfp';link.click();
    setTimeout(()=>URL.revokeObjectURL(url),1000);
  });
};

async function poll(){
  const revision=editRevision,wasIdle=pending===0&&!timer&&!replacing;
  try{
    const latest=await fetch('/api/state').then(r=>{if(!r.ok)throw Error('Панель недоступна');return r.json()});
    // An older GET must not undo the result of a more recent POST or local edit.
    if(wasIdle&&pending===0&&!timer&&!replacing&&revision===editRevision){
      data=latest;
      syncMixer();
    }
  }catch(e){error(e.message)}finally{setTimeout(poll,160)}
}
poll();
