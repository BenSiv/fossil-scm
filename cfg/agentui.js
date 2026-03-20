(function(){
  var sid = JSON.parse(document.body.dataset.sid || "0");
  var input = document.getElementById('agent-chat-input');
  var send = document.getElementById('agent-chat-send');
  var provider = document.getElementById('agent-provider');
  var model = document.getElementById('agent-model');
  var context = document.getElementById('agent-context');
  var statusBox = document.getElementById('agent-chat-status');
  var log = document.getElementById('agent-chat-log');
  var feedbackUseful = document.getElementById('agent-feedback-useful');
  var feedbackNotUseful = document.getElementById('agent-feedback-not-useful');
  var feedbackStatus = document.getElementById('agent-feedback-status');
  var pollHandle = 0;
  var lastAcid = 0;
  var lastReplyAcid = 0;
  var lastReplyFeedback = '';

  function esc(text){
    return (text || '').replace(/[&<>]/g, function(c){
      return {'&':'&amp;','<':'&lt;','>':'&gt;'}[c];
    });
  }
  function setStatus(text, status){
    var statusText = 'Status: ' + text;
    if(status==='running') statusText += ' <span class="spinner"></span>';
    if(statusBox) statusBox.innerHTML = statusText;
  }
  function setFeedbackState(acid, feedback){
    lastReplyAcid = acid || 0;
    lastReplyFeedback = feedback || '';
    if(feedbackUseful) feedbackUseful.disabled = !lastReplyAcid;
    if(feedbackNotUseful) feedbackNotUseful.disabled = !lastReplyAcid;
    if(feedbackStatus){
      feedbackStatus.textContent = lastReplyAcid ? ('Current: ' + (lastReplyFeedback || 'none')) : 'No reply selected';
    }
  }
  function appendEvent(msg){
    var meta;
    try{ meta = msg.meta ? JSON.parse(msg.meta) : {}; }catch(e){ meta = {}; }
    if(meta.hidden || msg.kind==='context') return;
    var div = document.createElement('div');
    div.style.marginBottom = '0.8em';
    var html = '<b>' + (msg.role==='user' ? 'You' : (msg.role==='system' ? 'System' : 'Agent')) + ':</b>';
    if(msg.provider) html += ' <span class="dimmed">[' + esc(msg.provider) + (msg.model ? ' / ' + esc(msg.model) : '') + ']</span>';
    if(msg.kind) html += ' <span class="dimmed">{' + esc(msg.kind) + '}</span>';
    if(meta.thinking) html += ' <details class="thinking-details"><summary class="dimmed">Reasoning</summary><pre style="white-space:pre-wrap;margin:0.5em 0;padding:0.5em;border-left:3px solid #ccc;background:rgba(0,0,0,0.02)">' + esc(meta.thinking) + '</pre></details>';
    html += ' <pre style="white-space:pre-wrap;display:inline;margin:0">' + esc(msg.msg || '') + '</pre>';
    div.innerHTML = html;
    log.appendChild(div);
    if(msg.acid && msg.acid>lastAcid) lastAcid = msg.acid;
    if(msg.role==='agent' && msg.kind==='reply') setFeedbackState(msg.acid || 0, msg.feedback || '');
  }
  function addMsg(role, text){
    var div = document.createElement('div');
    div.style.marginBottom = '0.8em';
    div.innerHTML = '<b>'+role+':</b> <pre style="white-space:pre-wrap;display:inline;margin:0">' + esc(text) + '</pre>';
    log.appendChild(div);
    log.scrollTop = log.scrollHeight;
  }
  
  send.addEventListener('click', function(){
    var msg = input.value.trim();
    if(!msg) return;
    setStatus('Sending request...', 'running');
    addMsg('You', msg);
    input.value = '';
    var params = {sid: sid, msg: msg};
    var url = 'agent-chat-stream?' + new URLSearchParams(params);
    var decoder = new TextDecoder();
    var partialLine = '';
    var agentDiv = null;
    var agentPre = null;
    var sawPayload = false;
    
    fetch(url).then(function(response){
      if(!response.ok){
        throw new Error('Stream request failed: HTTP ' + response.status);
      }
      if(!response.body){
        throw new Error('Stream request returned no body.');
      }
      var reader = response.body.getReader();
      function readChunk(){
        reader.read().then(function(result){
          if(result.done){
            if(sawPayload){
              setStatus('Reply received');
            }else{
              setStatus('Backend returned an empty reply', 'error');
              addMsg('System', 'Backend returned an empty reply.');
            }
            return;
          }
          var chunk = decoder.decode(result.value, {stream: true});
          var lines = (partialLine + chunk).split('\n');
          partialLine = lines.pop();
          lines.forEach(function(line){
            if(line.startsWith('data: ')){
              var dataStr = line.slice(6).trim();
              var content;
              try { content = JSON.parse(dataStr); } catch(e) { return; }
              
              if(content && typeof content === 'object' && content.error){
                setStatus('Backend error', 'error');
                addMsg('System', content.error);
                return;
              } else if(content && typeof content === 'object' && content.type === 'propose_edit'){
                renderApprovalCard(content);
              } else {
                if(!agentDiv){
                  agentDiv = document.createElement('div');
                  agentDiv.style.marginBottom = '0.8em';
                  agentDiv.innerHTML = '<b>Agent:</b> <pre style="white-space:pre-wrap;display:inline;margin:0"></pre>';
                  log.appendChild(agentDiv);
                  agentPre = agentDiv.querySelector('pre');
                }
                sawPayload = true;
                agentPre.textContent += content;
              }
              log.scrollTop = log.scrollHeight;
            }
          });
          readChunk();
        }).catch(function(err){
          setStatus('Stream failed', 'error');
          addMsg('System', err && err.message ? err.message : 'Stream read failed.');
        });
      }
      readChunk();
    }).catch(function(err){
      setStatus('Request failed', 'error');
      addMsg('System', err && err.message ? err.message : 'Request failed.');
    });
  });

  function renderApprovalCard(data){
    var div = document.createElement('div');
    div.className = 'approval-card';
    div.innerHTML = '<b>Proposed Edit:</b> <span class="dimmed">' + esc(data.path) + '</span>' +
      '<div class="diff-preview">' +
      '<div class="diff-del">' + esc(data.replace) + '</div>' +
      '<div class="diff-add">' + esc(data.with) + '</div>' +
      '</div>' +
      '<button class="btn-approve">Approve & Apply</button>' +
      '<button class="btn-reject">Reject</button>';
    
    div.querySelector('.btn-approve').onclick = function(){
       div.innerHTML = '<i>Applying edit...</i>';
       sendConfirmedEdit(data);
    };
    div.querySelector('.btn-reject').onclick = function(){
       div.remove();
       addMsg('System', 'Edit rejected by user.');
    };
    log.appendChild(div);
  }

  function sendConfirmedEdit(data){
    /* We send a special message that the orchestration script will recognize as an approval */
    var confirmMsg = 'CONFIRMED_EDIT: ' + JSON.stringify({
      tool: "edit_file",
      path: data.path,
      replace: data.replace,
      with: data.with,
      confirmed: 1
    });
    input.value = confirmMsg;
    send.click();
  }

  var toggle = document.getElementById('theme-toggle');
  var currentTheme = localStorage.getItem('agent-theme') || 'auto';
  if(currentTheme !== 'auto') document.body.classList.add('theme-' + currentTheme);

  toggle.addEventListener('click', function(){
    if(document.body.classList.contains('theme-dark')){
      document.body.classList.remove('theme-dark');
      document.body.classList.add('theme-light');
      localStorage.setItem('agent-theme', 'light');
    }else{
      document.body.classList.remove('theme-light');
      document.body.classList.add('theme-dark');
      localStorage.setItem('agent-theme', 'dark');
    }
  });

  log.scrollTop = log.scrollHeight;
})();
