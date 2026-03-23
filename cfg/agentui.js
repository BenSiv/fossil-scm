(function(){
  var sid = JSON.parse(document.body.dataset.sid || "0");
  var input = document.getElementById('agent-chat-input');
  var send = document.getElementById('agent-chat-send');
  var provider = document.getElementById('agent-provider');
  var model = document.getElementById('agent-model');
  var context = document.getElementById('agent-context');
  var statusBox = document.getElementById('agent-chat-status');
  var requestBox = document.getElementById('agent-chat-request');
  var log = document.getElementById('agent-chat-log');
  var feedbackUseful = document.getElementById('agent-feedback-useful');
  var feedbackNotUseful = document.getElementById('agent-feedback-not-useful');
  var feedbackStatus = document.getElementById('agent-feedback-status');
  var pollHandle = 0;
  var lastAcid = 0;
  var lastReplyAcid = 0;
  var lastReplyFeedback = '';
  var supportsStreaming = null;
  var configPromise = null;
  var currentRequestId = '';
  var currentRequestState = '';
  var currentRequestActive = false;
  var currentRequestTerminal = false;

  function ensureConfig(){
    if(configPromise) return configPromise;
    configPromise = fetch('agent-config?sid=' + encodeURIComponent(sid)).then(function(resp){
      if(!resp.ok) throw new Error('Config request failed: HTTP ' + resp.status);
      return resp.json();
    }).then(function(cfg){
      if(cfg && typeof cfg.chat_supports_streaming !== 'undefined'){
        supportsStreaming = !!cfg.chat_supports_streaming;
      }
      if(cfg && cfg.sid) sid = cfg.sid;
    }).catch(function(){
      supportsStreaming = false;
    });
    return configPromise;
  }

  function esc(text){
    return (text || '').replace(/[&<>]/g, function(c){
      return {'&':'&amp;','<':'&lt;','>':'&gt;'}[c];
    });
  }

  function isTerminalState(state){
    return state === 'finished' || state === 'failed' || state === 'cancelled' || state === 'reply' || state === 'error';
  }

  function requestIsTerminal(request){
    if(!request) return false;
    if(typeof request.is_terminal === 'boolean') return request.is_terminal;
    return isTerminalState(request.state || '');
  }

  function requestIsActive(request){
    if(!request) return false;
    if(typeof request.is_active === 'boolean') return request.is_active;
    return !requestIsTerminal(request);
  }

  function setStatus(text, status){
    var statusText = 'Status: ' + text;
    if(status==='running') statusText += ' <span class="spinner"></span>';
    if(statusBox) statusBox.innerHTML = statusText;
  }

  function renderRequestBox(request){
    if(!requestBox) return;
    if(!request || !request.request_id){
      requestBox.style.display = 'none';
      requestBox.innerHTML = '';
      return;
    }
    requestBox.style.display = '';
    requestBox.innerHTML =
      '<b>Request:</b> <code>' + esc(request.request_id) + '</code>' +
      '<span class="agent-request-state">' + esc(request.state || '') + '</span>' +
      '<span class="agent-request-ref dimmed">active=' + esc(String(!!requestIsActive(request))) + '</span>' +
      '<span class="agent-request-ref dimmed">terminal=' + esc(String(!!requestIsTerminal(request))) + '</span>' +
      (request.ctime ? ' <span class="dimmed">started ' + esc(request.ctime) + '</span>' : '') +
      (request.terminal_acid ? ' <span class="dimmed">reply acid ' + esc(String(request.terminal_acid)) + '</span>' : '');
  }

  function updateRequestState(request){
    if(!request) return;
    currentRequestId = request.request_id || currentRequestId;
    currentRequestState = request.state || currentRequestState;
    currentRequestActive = requestIsActive(request);
    currentRequestTerminal = requestIsTerminal(request);
    renderRequestBox(request);
    if(currentRequestState){
      setStatus('Request ' + currentRequestState, currentRequestActive ? 'running' : '');
    }
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
    var div;
    var html;
    try{ meta = msg.meta ? JSON.parse(msg.meta) : {}; }catch(e){ meta = {}; }
    if(meta.hidden || msg.kind==='context') return;
    div = document.createElement('div');
    div.style.marginBottom = '0.8em';
    html = '<b>' + (msg.role==='user' ? 'You' : (msg.role==='system' ? 'System' : 'Agent')) + ':</b>';
    if(msg.provider) html += ' <span class="dimmed">[' + esc(msg.provider) + (msg.model ? ' / ' + esc(msg.model) : '') + ']</span>';
    if(msg.event_type) html += ' <span class="agent-event-type">' + esc(msg.event_type) + '</span>';
    else if(msg.kind) html += ' <span class="dimmed">{' + esc(msg.kind) + '}</span>';
    if(msg.request_id) html += ' <span class="agent-request-ref dimmed">#' + esc(msg.request_id) + '</span>';
    if(msg.tool && msg.tool.name){
      html += ' <span class="agent-request-ref dimmed">tool=' + esc(msg.tool.name) + '</span>';
      if(msg.tool_phase) html += ' <span class="agent-request-ref dimmed">phase=' + esc(msg.tool_phase) + '</span>';
    }
    if(meta.thinking){
      html += ' <details class="thinking-details"><summary class="dimmed">Reasoning</summary><pre style="white-space:pre-wrap;margin:0.5em 0;padding:0.5em;border-left:3px solid #ccc;background:rgba(0,0,0,0.02)">' + esc(meta.thinking) + '</pre></details>';
    }
    html += ' <pre style="white-space:pre-wrap;display:inline;margin:0">' + esc(msg.msg || '') + '</pre>';
    div.innerHTML = html;
    log.appendChild(div);
    if(msg.acid && msg.acid>lastAcid) lastAcid = msg.acid;
    if(msg.role==='agent' && msg.kind==='reply') setFeedbackState(msg.acid || 0, msg.feedback || '');
    if(msg.request_id && (!currentRequestId || currentRequestId === msg.request_id)){
      currentRequestId = msg.request_id;
    }
    log.scrollTop = log.scrollHeight;
  }

  function addMsg(role, text){
    var div = document.createElement('div');
    div.style.marginBottom = '0.8em';
    div.innerHTML = '<b>'+role+':</b> <pre style="white-space:pre-wrap;display:inline;margin:0">' + esc(text) + '</pre>';
    log.appendChild(div);
    log.scrollTop = log.scrollHeight;
  }

  function stopPolling(){
    if(pollHandle){
      clearTimeout(pollHandle);
      pollHandle = 0;
    }
  }

  function schedulePoll(delayMs){
    stopPolling();
    pollHandle = setTimeout(pollEvents, delayMs || 1200);
  }

  function pollEvents(){
    if(!sid) return;
    fetch('agent-api-v1-events?sid=' + encodeURIComponent(sid) + '&after=' + encodeURIComponent(lastAcid))
      .then(function(resp){
        if(!resp.ok) throw new Error('Event poll failed: HTTP ' + resp.status);
        return resp.json();
      })
      .then(function(data){
        var request = data && data.request ? data.request : null;
        if(data && Array.isArray(data.events)){
          data.events.forEach(appendEvent);
        }
        if(typeof data.last_acid === 'number' && data.last_acid > lastAcid){
          lastAcid = data.last_acid;
        }
        if(request) updateRequestState(request);
        if(request && requestIsActive(request)){
          schedulePoll(1000);
        }else if(currentRequestId && currentRequestActive && !currentRequestTerminal){
          schedulePoll(1000);
        }
      })
      .catch(function(err){
        setStatus('Event polling failed', 'error');
        addMsg('System', err && err.message ? err.message : 'Event polling failed.');
      });
  }

  send.addEventListener('click', function(){
    var msg = input.value.trim();
    if(!msg) return;
    setStatus('Sending request...', 'running');
    addMsg('You', msg);
    input.value = '';
    ensureConfig().then(function(){
      if(supportsStreaming){
        sendStream(msg);
      }else{
        sendJson(msg);
      }
    });
  });

  function sendStream(msg){
    var params = {sid: sid, msg: msg, provider: provider.value, model: model.value, context: context.checked ? 1 : 0};
    var url = 'agent-chat-stream?' + new URLSearchParams(params);
    var decoder = new TextDecoder();
    var partialLine = '';
    var agentDiv = null;
    var agentPre = null;
    var sawPayload = false;

    fetch(url).then(function(response){
      if(!response.ok) throw new Error('Stream request failed: HTTP ' + response.status);
      if(!response.body) throw new Error('Stream request returned no body.');
      var reader = response.body.getReader();
      function readChunk(){
        reader.read().then(function(result){
          if(result.done){
            schedulePoll(200);
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
                sawPayload = true;
                setStatus('Backend error', 'error');
                addMsg('System', content.error);
              }else if(content && typeof content === 'object' && content.type === 'propose_edit'){
                renderApprovalCard(content);
              }else{
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
  }

  function sendJson(msg){
    var params = new URLSearchParams({
      sid: sid,
      msg: msg,
      provider: provider.value,
      model: model.value,
      context: context.checked ? 1 : 0
    });
    fetch('agent-api-v1-chat', {
      method: 'POST',
      headers: {'Content-Type': 'application/x-www-form-urlencoded; charset=UTF-8'},
      body: params.toString()
    }).then(function(response){
      if(!response.ok){
        throw new Error('Request failed: HTTP ' + response.status);
      }
      return response.json();
    }).then(function(data){
      if(data && data.error){
        setStatus('Backend error', 'error');
        addMsg('System', data.error);
        return;
      }
      if(data && data.chat && data.chat.sid) sid = data.chat.sid;
      if(data && data.request) updateRequestState(data.request);
      if(data && data.chat && typeof data.chat.reply === 'string'){
        addMsg('Agent', data.chat.reply);
      }
      schedulePoll(200);
    }).catch(function(err){
      setStatus('Request failed', 'error');
      addMsg('System', err && err.message ? err.message : 'Request failed.');
    });
  }

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
