(function () {
  'use strict';

  if (window.__espeed32ChatWidgetLoaded) {
    return;
  }
  window.__espeed32ChatWidgetLoaded = true;

  var CURRENT_SCRIPT = document.currentScript || null;
  var TURNSTILE_SRC = 'https://challenges.cloudflare.com/turnstile/v0/api.js';
  var TURNSTILE_TOKEN_STALE_MS = 4 * 60 * 1000;
  var MAX_MESSAGE_LENGTH = 500;
  var MAX_STORED_MESSAGES = 24;
  var MAX_REQUEST_HISTORY_MESSAGES = 12;
  var STORAGE_PREFIX = 'espeed32-chat-widget';
  var PLACEHOLDER_SITE_KEY = 'YOUR_TURNSTILE_SITE_KEY';

  var DEFAULT_CONFIG = {
    apiUrl: 'https://api.espeed32.com/api/chat',
    workerApiUrl: 'https://espeed32-chat-api.roy-266.workers.dev/api/chat',
    turnstileSiteKey: '0x4AAAAAACtvFdsbjONXtzrH',
    demoMode: false,
    workerMode: false
  };

  var I18N = {
    en: {
      launcherLabel: 'Help',
      panelTitle: 'DRIVING ASSISTANT',
      panelSubtitle: 'Ask about setup, flashing, settings, or troubleshooting.',
      inputPlaceholder: 'Write your message...',
      sendLabel: 'Send',
      closeLabel: 'Close help',
      openLabel: 'Open help',
      verificationLabel: 'Verification',
      verificationRequired: 'Please complete the verification before sending.',
      verificationExpired: 'Verification expired. Please try again.',
      verificationError: 'Verification could not be loaded. Refresh and try again.',
      verificationLocalHint: 'Check that the Turnstile sitekey allows this hostname.',
      demoModeNotice: 'Demo mode is active. Replies are simulated in the browser.',
      workerModeNotice: 'Worker test mode is active. Requests go directly to workers.dev.',
      workerModeLocalHint: 'If live requests still fail here, allow this origin in backend CORS and Turnstile.',
      configError: 'Set YOUR_TURNSTILE_SITE_KEY in /ui/chat-widget.js to enable chat.',
      tooLong: 'Messages can be up to 500 characters.',
      networkError: 'Something went wrong. Please try again.',
      apiUnreachable: 'Could not reach the chat API. Please try again.',
      apiUnreachableLocal: 'Could not reach the chat API. Check DNS, CORS, and whether api.espeed32.com points to your backend yet.',
      api400: 'Please check your message and try again.',
      api403: 'The chat backend rejected the verification token.',
      api429: 'Too many requests. Please wait a moment and try again.',
      api502: 'The chat service is temporarily unavailable. Please try again.',
      api500: 'The chat service returned a server error. Please try again.',
      apiUnexpected: 'The chat service returned an unexpected response.',
      api403LocalHint: 'Check TURNSTILE_SECRET_KEY validation and allowed hostnames in the backend.',
      sourcesLabel: 'Sources',
      loadingLabel: 'Thinking...',
      offlineLabel: 'You appear to be offline.',
      welcomeMessage: 'Hi. Ask me about ESPEED32 setup, flashing, tuning, or troubleshooting.'
    },
    no: {
      launcherLabel: 'Help',
      panelTitle: 'DRIVING ASSISTANT',
      panelSubtitle: 'Spor om oppsett, flashing, innstillinger eller feilsoking.',
      inputPlaceholder: 'Skriv meldingen din...',
      sendLabel: 'Send',
      closeLabel: 'Lukk hjelp',
      openLabel: 'Apne hjelp',
      verificationLabel: 'Verifisering',
      verificationRequired: 'Fullfor verifiseringen for du sender.',
      verificationExpired: 'Verifiseringen utlop. Prov igjen.',
      verificationError: 'Verifiseringen kunne ikke lastes. Oppdater siden og prov igjen.',
      verificationLocalHint: 'Sjekk at Turnstile-sitekeyen tillater dette hostnavnet.',
      demoModeNotice: 'Demo-modus er aktiv. Svarene simuleres i nettleseren.',
      workerModeNotice: 'Worker-testmodus er aktiv. Foresporsler gaar direkte til workers.dev.',
      workerModeLocalHint: 'Hvis live-requests fortsatt feiler her, maa denne originen tillates i backend-CORS og Turnstile.',
      configError: 'Sett YOUR_TURNSTILE_SITE_KEY i /ui/chat-widget.js for aa aktivere chat.',
      tooLong: 'Meldinger kan vaere opptil 500 tegn.',
      networkError: 'Something went wrong. Please try again.',
      apiUnreachable: 'Kunne ikke na chat-API-et. Prov igjen.',
      apiUnreachableLocal: 'Kunne ikke na chat-API-et. Sjekk DNS, CORS og om api.espeed32.com peker til backend enna.',
      api400: 'Sjekk meldingen din og prov igjen.',
      api403: 'Chat-backenden avviste verifiseringstokenet.',
      api429: 'For mange foresporsler. Vent litt og prov igjen.',
      api502: 'Chat-tjenesten er midlertidig utilgjengelig. Prov igjen om litt.',
      api500: 'Chat-tjenesten svarte med serverfeil. Prov igjen.',
      apiUnexpected: 'Chat-tjenesten svarte med et uventet format.',
      api403LocalHint: 'Sjekk TURNSTILE_SECRET_KEY-validering og tillatte hostnavn i backenden.',
      sourcesLabel: 'Kilder',
      loadingLabel: 'Tenker...',
      offlineLabel: 'Det ser ut som du er offline.',
      welcomeMessage: 'Hei. Sporr meg om ESPEED32-oppsett, flashing, tuning eller feilsoking.'
    },
    es: {
      launcherLabel: 'Help',
      panelTitle: 'DRIVING ASSISTANT',
      panelSubtitle: 'Pregunta sobre configuracion, flasheo, ajustes o solucion de problemas.',
      inputPlaceholder: 'Escribe tu mensaje...',
      sendLabel: 'Enviar',
      closeLabel: 'Cerrar ayuda',
      openLabel: 'Abrir ayuda',
      verificationLabel: 'Verificacion',
      verificationRequired: 'Completa la verificacion antes de enviar.',
      verificationExpired: 'La verificacion expiro. Intentalo de nuevo.',
      verificationError: 'No se pudo cargar la verificacion. Recarga la pagina e intentalo otra vez.',
      verificationLocalHint: 'Check that the Turnstile sitekey allows this hostname.',
      demoModeNotice: 'Demo mode is active. Replies are simulated in the browser.',
      workerModeNotice: 'Worker test mode is active. Requests go directly to workers.dev.',
      workerModeLocalHint: 'If live requests still fail here, allow this origin in backend CORS and Turnstile.',
      configError: 'Configura YOUR_TURNSTILE_SITE_KEY en /ui/chat-widget.js para activar el chat.',
      tooLong: 'Los mensajes pueden tener hasta 500 caracteres.',
      networkError: 'Something went wrong. Please try again.',
      apiUnreachable: 'Could not reach the chat API. Please try again.',
      apiUnreachableLocal: 'Could not reach the chat API. Check DNS, CORS, and whether api.espeed32.com points to your backend yet.',
      api400: 'Revisa tu mensaje y vuelve a intentarlo.',
      api403: 'The chat backend rejected the verification token.',
      api429: 'Demasiadas solicitudes. Espera un momento y vuelve a intentarlo.',
      api502: 'El servicio de chat no esta disponible temporalmente. Intentalo de nuevo.',
      api500: 'The chat service returned a server error. Please try again.',
      apiUnexpected: 'The chat service returned an unexpected response.',
      api403LocalHint: 'Check TURNSTILE_SECRET_KEY validation and allowed hostnames in the backend.',
      sourcesLabel: 'Fuentes',
      loadingLabel: 'Pensando...',
      offlineLabel: 'Parece que no tienes conexion.',
      welcomeMessage: 'Hola. Preguntame sobre configuracion, flasheo, ajuste o diagnostico de ESPEED32.'
    },
    de: {
      launcherLabel: 'Help',
      panelTitle: 'DRIVING ASSISTANT',
      panelSubtitle: 'Frage nach Setup, Flashing, Einstellungen oder Fehlersuche.',
      inputPlaceholder: 'Nachricht schreiben...',
      sendLabel: 'Senden',
      closeLabel: 'Hilfe schliessen',
      openLabel: 'Hilfe oeffnen',
      verificationLabel: 'Verifizierung',
      verificationRequired: 'Bitte schliesse die Verifizierung vor dem Senden ab.',
      verificationExpired: 'Die Verifizierung ist abgelaufen. Bitte erneut versuchen.',
      verificationError: 'Die Verifizierung konnte nicht geladen werden. Seite neu laden und erneut versuchen.',
      verificationLocalHint: 'Check that the Turnstile sitekey allows this hostname.',
      demoModeNotice: 'Demo mode is active. Replies are simulated in the browser.',
      workerModeNotice: 'Worker test mode is active. Requests go directly to workers.dev.',
      workerModeLocalHint: 'If live requests still fail here, allow this origin in backend CORS and Turnstile.',
      configError: 'Trage YOUR_TURNSTILE_SITE_KEY in /ui/chat-widget.js ein, um den Chat zu aktivieren.',
      tooLong: 'Nachrichten duerfen hoechstens 500 Zeichen lang sein.',
      networkError: 'Something went wrong. Please try again.',
      apiUnreachable: 'Could not reach the chat API. Please try again.',
      apiUnreachableLocal: 'Could not reach the chat API. Check DNS, CORS, and whether api.espeed32.com points to your backend yet.',
      api400: 'Bitte pruefe deine Nachricht und versuche es erneut.',
      api403: 'The chat backend rejected the verification token.',
      api429: 'Zu viele Anfragen. Bitte kurz warten und erneut versuchen.',
      api502: 'Der Chat-Dienst ist voruebergehend nicht verfuegbar. Bitte spaeter erneut versuchen.',
      api500: 'The chat service returned a server error. Please try again.',
      apiUnexpected: 'The chat service returned an unexpected response.',
      api403LocalHint: 'Check TURNSTILE_SECRET_KEY validation and allowed hostnames in the backend.',
      sourcesLabel: 'Quellen',
      loadingLabel: 'Denkt nach...',
      offlineLabel: 'Es sieht so aus, als ob du offline bist.',
      welcomeMessage: 'Hallo. Frag mich nach ESPEED32-Setup, Flashing, Tuning oder Fehlersuche.'
    },
    it: {
      launcherLabel: 'Help',
      panelTitle: 'DRIVING ASSISTANT',
      panelSubtitle: 'Chiedi di configurazione, flashing, impostazioni o risoluzione dei problemi.',
      inputPlaceholder: 'Scrivi il tuo messaggio...',
      sendLabel: 'Invia',
      closeLabel: 'Chiudi aiuto',
      openLabel: 'Apri aiuto',
      verificationLabel: 'Verifica',
      verificationRequired: 'Completa la verifica prima di inviare.',
      verificationExpired: 'La verifica e scaduta. Riprova.',
      verificationError: 'Impossibile caricare la verifica. Aggiorna la pagina e riprova.',
      verificationLocalHint: 'Check that the Turnstile sitekey allows this hostname.',
      demoModeNotice: 'Demo mode is active. Replies are simulated in the browser.',
      workerModeNotice: 'Worker test mode is active. Requests go directly to workers.dev.',
      workerModeLocalHint: 'If live requests still fail here, allow this origin in backend CORS and Turnstile.',
      configError: 'Inserisci YOUR_TURNSTILE_SITE_KEY in /ui/chat-widget.js per attivare la chat.',
      tooLong: 'I messaggi possono contenere al massimo 500 caratteri.',
      networkError: 'Something went wrong. Please try again.',
      apiUnreachable: 'Could not reach the chat API. Please try again.',
      apiUnreachableLocal: 'Could not reach the chat API. Check DNS, CORS, and whether api.espeed32.com points to your backend yet.',
      api400: 'Controlla il messaggio e riprova.',
      api403: 'The chat backend rejected the verification token.',
      api429: 'Troppe richieste. Attendi un momento e riprova.',
      api502: 'Il servizio chat e temporaneamente non disponibile. Riprova piu tardi.',
      api500: 'The chat service returned a server error. Please try again.',
      apiUnexpected: 'The chat service returned an unexpected response.',
      api403LocalHint: 'Check TURNSTILE_SECRET_KEY validation and allowed hostnames in the backend.',
      sourcesLabel: 'Fonti',
      loadingLabel: 'Sta pensando...',
      offlineLabel: 'Sembra che tu sia offline.',
      welcomeMessage: 'Ciao. Chiedimi di configurazione, flashing, tuning o risoluzione problemi di ESPEED32.'
    }
  };

  function mergeObjects(base, extra) {
    var merged = {};
    var key;
    for (key in base) {
      if (Object.prototype.hasOwnProperty.call(base, key)) {
        merged[key] = base[key];
      }
    }
    if (!extra || typeof extra !== 'object') {
      return merged;
    }
    for (key in extra) {
      if (Object.prototype.hasOwnProperty.call(extra, key)) {
        merged[key] = extra[key];
      }
    }
    return merged;
  }

  function isPlaceholderSiteKey(value) {
    return !value || value === PLACEHOLDER_SITE_KEY;
  }

  function hasDemoQueryFlag() {
    try {
      return new URLSearchParams(window.location.search).get('chat_demo') === '1';
    } catch (_) {
      return false;
    }
  }

  function hasWorkerQueryFlag() {
    try {
      return new URLSearchParams(window.location.search).get('chat_worker') === '1';
    } catch (_) {
      return false;
    }
  }

  function isLocalDevHost() {
    var hostname = (window.location && window.location.hostname || '').toLowerCase();
    return hostname === 'localhost' ||
      hostname === '127.0.0.1' ||
      hostname === '::1' ||
      hostname === '[::1]';
  }

  function withHint(message, hint) {
    if (!hint) {
      return message;
    }
    return message + ' ' + hint;
  }

  function readBackendDetail(payload) {
    if (!payload || typeof payload !== 'object') {
      return '';
    }
    if (typeof payload.error === 'string' && payload.error.trim()) {
      return payload.error.trim();
    }
    if (typeof payload.message === 'string' && payload.message.trim()) {
      return payload.message.trim();
    }
    return '';
  }

  function wait(ms) {
    return new Promise(function (resolve) {
      window.setTimeout(resolve, ms);
    });
  }

  function normalizeLanguageTag(lang) {
    var value = String(lang || '').toLowerCase();
    if (!value) {
      return '';
    }
    if (value.indexOf('no') === 0 || value.indexOf('nb') === 0 || value.indexOf('nn') === 0) {
      return 'no';
    }
    if (value.indexOf('es') === 0) {
      return 'es';
    }
    if (value.indexOf('de') === 0) {
      return 'de';
    }
    if (value.indexOf('it') === 0) {
      return 'it';
    }
    if (value.indexOf('en') === 0) {
      return 'en';
    }
    return '';
  }

  function resolveLanguage() {
    var candidates = [];
    var htmlLang = '';
    var i;
    var resolved;

    if (typeof navigator !== 'undefined') {
      if (Array.isArray(navigator.languages) && navigator.languages.length) {
        candidates = candidates.concat(navigator.languages);
      }
      if (navigator.language) {
        candidates.push(navigator.language);
      }
    }

    if (document.documentElement) {
      htmlLang = document.documentElement.getAttribute('lang') || '';
      if (htmlLang) {
        candidates.push(htmlLang);
      }
    }

    candidates.push('en');

    for (i = 0; i < candidates.length; i += 1) {
      resolved = normalizeLanguageTag(candidates[i]);
      if (resolved) {
        return resolved;
      }
    }

    return 'en';
  }

  function safeJsonParse(value, fallbackValue) {
    if (typeof value !== 'string' || !value) {
      return fallbackValue;
    }
    try {
      return JSON.parse(value);
    } catch (_) {
      return fallbackValue;
    }
  }

  function createStorage(prefix) {
    function getKey(key) {
      return prefix + ':' + key;
    }

    return {
      get: function (key, fallbackValue) {
        try {
          var raw = window.localStorage.getItem(getKey(key));
          return raw === null ? fallbackValue : safeJsonParse(raw, fallbackValue);
        } catch (_) {
          return fallbackValue;
        }
      },
      set: function (key, value) {
        try {
          window.localStorage.setItem(getKey(key), JSON.stringify(value));
          return true;
        } catch (_) {
          return false;
        }
      }
    };
  }

  function ensureStylesheet() {
    var cssHref = '/ui/chat-widget.css';
    if (CURRENT_SCRIPT && CURRENT_SCRIPT.src) {
      cssHref = CURRENT_SCRIPT.src.replace(/chat-widget\.js(?:\?.*)?$/, 'chat-widget.css');
    }
    if (document.querySelector('link[data-espeed32-chat-widget-style]')) {
      return;
    }
    var link = document.createElement('link');
    link.rel = 'stylesheet';
    link.href = cssHref;
    link.setAttribute('data-espeed32-chat-widget-style', 'true');
    document.head.appendChild(link);
  }

  function loadTurnstileScript() {
    if (window.turnstile && typeof window.turnstile.render === 'function') {
      return Promise.resolve(window.turnstile);
    }
    if (window.__espeed32ChatTurnstilePromise) {
      return window.__espeed32ChatTurnstilePromise;
    }
    window.__espeed32ChatTurnstilePromise = new Promise(function (resolve, reject) {
      var existing = document.querySelector('script[data-espeed32-chat-turnstile]');
      if (existing) {
        if (window.turnstile && typeof window.turnstile.render === 'function') {
          resolve(window.turnstile);
          return;
        }
        existing.addEventListener('load', function () {
          resolve(window.turnstile);
        }, { once: true });
        existing.addEventListener('error', function () {
          reject(new Error('Turnstile failed to load'));
        }, { once: true });
        return;
      }
      var script = document.createElement('script');
      script.src = TURNSTILE_SRC;
      script.async = true;
      script.defer = true;
      script.setAttribute('data-espeed32-chat-turnstile', 'true');
      script.onload = function () {
        if (window.turnstile && typeof window.turnstile.render === 'function') {
          resolve(window.turnstile);
        } else {
          reject(new Error('Turnstile unavailable'));
        }
      };
      script.onerror = function () {
        reject(new Error('Turnstile failed to load'));
      };
      document.head.appendChild(script);
    });
    return window.__espeed32ChatTurnstilePromise;
  }

  function Espeed32ChatWidget(config) {
    this.config = config;
    this.lang = resolveLanguage();
    this.text = I18N[this.lang] || I18N.en;
    this.storage = createStorage(STORAGE_PREFIX);
    this.state = {
      isOpen: false,
      isSending: false,
      turnstileToken: '',
      turnstileTokenIssuedAt: 0,
      turnstileWidgetId: null,
      lastTurnstileTokenUsed: '',
      messages: []
    };
    this.refs = {};
  }

  Espeed32ChatWidget.prototype.isDemoMode = function () {
    return !!this.config.demoMode;
  };

  Espeed32ChatWidget.prototype.isWorkerMode = function () {
    return !!this.config.workerMode;
  };

  Espeed32ChatWidget.prototype.init = function () {
    ensureStylesheet();
    this.restoreState();
    this.mount();
    this.bindEvents();
    this.renderMessages();
    this.renderComposerState();
    this.updateCounter();
    if (this.state.isOpen) {
      this.open(false);
    } else {
      this.close(false);
    }
    this.initTurnstile();
  };

  Espeed32ChatWidget.prototype.restoreState = function () {
    var savedOpen = this.storage.get('open', false);
    var savedMessages = this.storage.get('messages', []);

    this.state.isOpen = !!savedOpen;
    if (Array.isArray(savedMessages) && savedMessages.length) {
      this.state.messages = savedMessages.filter(function (message) {
        return message &&
          (message.role === 'assistant' || message.role === 'user') &&
          typeof message.text === 'string';
      }).slice(-MAX_STORED_MESSAGES).map(function (message) {
        return {
          role: message.role === 'user' ? 'user' : 'assistant',
          text: String(message.text || ''),
          sources: Array.isArray(message.sources) ? message.sources.slice(0, 6) : [],
          responseId: typeof message.responseId === 'string' ? message.responseId.trim() : ''
        };
      });
    }
    if (!this.state.messages.length) {
      this.state.messages = [{
        role: 'assistant',
        text: this.text.welcomeMessage,
        sources: [],
        responseId: ''
      }];
      this.persistMessages();
    }
  };

  Espeed32ChatWidget.prototype.mount = function () {
    var wrapper = document.createElement('div');
    wrapper.className = 'esw-widget';
    wrapper.innerHTML = '' +
      '<button class="esw-launcher" type="button" aria-expanded="false" aria-controls="esw-panel">' +
      '  <span class="esw-launcher-icon" aria-hidden="true">' + this.getLauncherIcon() + '</span>' +
      '  <span class="esw-launcher-text">' + this.text.launcherLabel + '</span>' +
      '</button>' +
      '<section id="esw-panel" class="esw-panel" aria-hidden="true">' +
      '  <header class="esw-header">' +
      '    <div class="esw-header-copy">' +
      '      <p class="esw-eyebrow">ESPEED32 HELP</p>' +
      '      <h2 class="esw-title">' + this.text.panelTitle + '</h2>' +
      '      <p class="esw-subtitle">' + this.text.panelSubtitle + '</p>' +
      '    </div>' +
      '    <button class="esw-close" type="button" aria-label="' + this.text.closeLabel + '">' + this.getCloseIcon() + '</button>' +
      '  </header>' +
      '  <div class="esw-status" hidden></div>' +
      '  <div class="esw-messages" role="log" aria-live="polite" aria-relevant="additions text"></div>' +
      '  <div class="esw-loading" hidden>' +
      '    <div class="esw-message esw-message-assistant esw-message-loading">' +
      '      <div class="esw-bubble">' +
      '        <span class="esw-typing-dot"></span>' +
      '        <span class="esw-typing-dot"></span>' +
      '        <span class="esw-typing-dot"></span>' +
      '        <span class="esw-loading-text">' + this.text.loadingLabel + '</span>' +
      '      </div>' +
      '    </div>' +
      '  </div>' +
      '  <div class="esw-turnstile-shell">' +
      '    <div class="esw-turnstile-copy">' +
      '      <span class="esw-turnstile-label">' + this.text.verificationLabel + '</span>' +
      '    </div>' +
      '    <div class="esw-turnstile" aria-live="polite"></div>' +
      '  </div>' +
      '  <form class="esw-form" novalidate>' +
      '    <label class="esw-sr-only" for="esw-input">' + this.text.inputPlaceholder + '</label>' +
      '    <textarea id="esw-input" class="esw-input" rows="1" maxlength="' + MAX_MESSAGE_LENGTH + '" placeholder="' + this.text.inputPlaceholder + '"></textarea>' +
      '    <div class="esw-form-row">' +
      '      <span class="esw-counter" aria-live="polite">0/' + MAX_MESSAGE_LENGTH + '</span>' +
      '      <button class="esw-send" type="submit">' + this.text.sendLabel + '</button>' +
      '    </div>' +
      '  </form>' +
      '</section>';

    document.body.appendChild(wrapper);

    this.refs.root = wrapper;
    this.refs.launcher = wrapper.querySelector('.esw-launcher');
    this.refs.panel = wrapper.querySelector('.esw-panel');
    this.refs.close = wrapper.querySelector('.esw-close');
    this.refs.status = wrapper.querySelector('.esw-status');
    this.refs.messages = wrapper.querySelector('.esw-messages');
    this.refs.loading = wrapper.querySelector('.esw-loading');
    this.refs.turnstile = wrapper.querySelector('.esw-turnstile');
    this.refs.turnstileShell = wrapper.querySelector('.esw-turnstile-shell');
    this.refs.form = wrapper.querySelector('.esw-form');
    this.refs.input = wrapper.querySelector('.esw-input');
    this.refs.counter = wrapper.querySelector('.esw-counter');
    this.refs.send = wrapper.querySelector('.esw-send');
  };

  Espeed32ChatWidget.prototype.bindEvents = function () {
    var self = this;

    this.refs.launcher.addEventListener('click', function () {
      if (self.state.isOpen) {
        self.close(true);
      } else {
        self.open(true);
      }
    });

    this.refs.close.addEventListener('click', function () {
      self.close(true);
    });

    this.refs.form.addEventListener('submit', function (event) {
      event.preventDefault();
      self.submitMessage();
    });

    this.refs.input.addEventListener('input', function () {
      self.updateCounter();
      self.autoResizeInput();
      if (self.refs.status.dataset.kind === 'error') {
        self.clearStatus();
      }
    });

    this.refs.input.addEventListener('keydown', function (event) {
      if (event.key === 'Enter' && !event.shiftKey) {
        event.preventDefault();
        self.submitMessage();
      }
    });

    document.addEventListener('keydown', function (event) {
      if (event.key === 'Escape' && self.state.isOpen) {
        self.close(true);
      }
    });
  };

  Espeed32ChatWidget.prototype.getLauncherIcon = function () {
    return '' +
      '<svg viewBox="0 0 24 24" focusable="false" aria-hidden="true">' +
      '  <path d="M12 3c-4.97 0-9 3.58-9 8 0 2.39 1.19 4.53 3.08 6A6.95 6.95 0 0 1 4.5 21l3.33-1.12A10.37 10.37 0 0 0 12 20c4.97 0 9-3.58 9-8s-4.03-8-9-8Zm-4 8.5a1.5 1.5 0 1 1 0 .01Zm4 0a1.5 1.5 0 1 1 0 .01Zm4 0a1.5 1.5 0 1 1 0 .01Z"></path>' +
      '</svg>';
  };

  Espeed32ChatWidget.prototype.getCloseIcon = function () {
    return '' +
      '<svg viewBox="0 0 24 24" focusable="false" aria-hidden="true">' +
      '  <path d="M6.7 5.3 12 10.6l5.3-5.3 1.4 1.4L13.4 12l5.3 5.3-1.4 1.4L12 13.4l-5.3 5.3-1.4-1.4 5.3-5.3-5.3-5.3Z"></path>' +
      '</svg>';
  };

  Espeed32ChatWidget.prototype.renderMessages = function () {
    var self = this;
    this.refs.messages.innerHTML = '';
    this.state.messages.forEach(function (message) {
      self.refs.messages.appendChild(self.createMessageNode(message));
    });
    this.scrollToBottom();
  };

  Espeed32ChatWidget.prototype.createMessageNode = function (message) {
    var item = document.createElement('article');
    var bubble = document.createElement('div');
    var body = document.createElement('div');

    item.className = 'esw-message ' + (message.role === 'user' ? 'esw-message-user' : 'esw-message-assistant');
    bubble.className = 'esw-bubble';
    body.className = 'esw-text';
    body.textContent = message.text;

    bubble.appendChild(body);

    if (message.role === 'assistant' && Array.isArray(message.sources) && message.sources.length) {
      var sourcesNode = this.createSourcesNode(message.sources);
      if (sourcesNode) {
        bubble.appendChild(sourcesNode);
      }
    }

    item.appendChild(bubble);
    return item;
  };

  Espeed32ChatWidget.prototype.createSourcesNode = function (sources) {
    var wrapper = document.createElement('div');
    var label = document.createElement('div');
    var list = document.createElement('div');

    wrapper.className = 'esw-sources';
    label.className = 'esw-sources-label';
    label.textContent = this.text.sourcesLabel;
    list.className = 'esw-sources-list';

    sources.forEach(function (source) {
      if (!source || typeof source.url !== 'string' || typeof source.title !== 'string') {
        return;
      }
      var link = document.createElement('a');
      link.className = 'esw-source-link';
      link.href = source.url;
      link.target = '_blank';
      link.rel = 'noopener noreferrer';
      link.textContent = source.title;
      list.appendChild(link);
    });

    if (!list.children.length) {
      return null;
    }

    wrapper.appendChild(label);
    wrapper.appendChild(list);
    return wrapper;
  };

  Espeed32ChatWidget.prototype.addMessage = function (message, persist) {
    this.state.messages.push({
      role: message.role === 'user' ? 'user' : 'assistant',
      text: String(message.text || ''),
      sources: Array.isArray(message.sources) ? message.sources.slice(0, 6) : [],
      responseId: typeof message.responseId === 'string' ? message.responseId.trim() : ''
    });
    if (this.state.messages.length > MAX_STORED_MESSAGES) {
      this.state.messages = this.state.messages.slice(-MAX_STORED_MESSAGES);
    }
    this.refs.messages.appendChild(this.createMessageNode(this.state.messages[this.state.messages.length - 1]));
    if (persist) {
      this.persistMessages();
    }
    this.scrollToBottom();
  };

  Espeed32ChatWidget.prototype.persistMessages = function () {
    this.storage.set('messages', this.state.messages);
  };

  Espeed32ChatWidget.prototype.getRequestHistory = function () {
    return this.state.messages
      .filter(function (message) {
        return message &&
          (message.role === 'assistant' || message.role === 'user') &&
          typeof message.text === 'string' &&
          message.text.trim();
      })
      .slice(-MAX_REQUEST_HISTORY_MESSAGES)
      .map(function (message) {
        return {
          role: message.role === 'user' ? 'user' : 'assistant',
          content: String(message.text || '').trim()
        };
      });
  };

  Espeed32ChatWidget.prototype.getPreviousResponseId = function () {
    var index;
    var message;

    for (index = this.state.messages.length - 1; index >= 0; index -= 1) {
      message = this.state.messages[index];
      if (
        message &&
        message.role === 'assistant' &&
        typeof message.responseId === 'string' &&
        message.responseId.trim()
      ) {
        return message.responseId.trim();
      }
    }

    return '';
  };

  Espeed32ChatWidget.prototype.open = function (persist) {
    this.state.isOpen = true;
    this.refs.root.classList.add('is-open');
    this.refs.launcher.setAttribute('aria-expanded', 'true');
    this.refs.launcher.setAttribute('aria-label', this.text.closeLabel);
    this.refs.panel.setAttribute('aria-hidden', 'false');
    if (persist) {
      this.storage.set('open', true);
    }
    this.scrollToBottom();
    window.setTimeout(this.refs.input.focus.bind(this.refs.input), 80);
  };

  Espeed32ChatWidget.prototype.close = function (persist) {
    this.state.isOpen = false;
    this.refs.root.classList.remove('is-open');
    this.refs.launcher.setAttribute('aria-expanded', 'false');
    this.refs.launcher.setAttribute('aria-label', this.text.openLabel);
    this.refs.panel.setAttribute('aria-hidden', 'true');
    if (persist) {
      this.storage.set('open', false);
    }
  };

  Espeed32ChatWidget.prototype.showStatus = function (message, kind, source) {
    this.refs.status.hidden = false;
    this.refs.status.textContent = message;
    this.refs.status.dataset.kind = kind || 'info';
    this.refs.status.dataset.source = source || '';
  };

  Espeed32ChatWidget.prototype.clearStatus = function () {
    this.refs.status.hidden = true;
    this.refs.status.textContent = '';
    this.refs.status.dataset.kind = '';
    this.refs.status.dataset.source = '';
  };

  Espeed32ChatWidget.prototype.showLoading = function (show) {
    this.refs.loading.hidden = !show;
    this.scrollToBottom();
  };

  Espeed32ChatWidget.prototype.scrollToBottom = function () {
    var self = this;
    window.requestAnimationFrame(function () {
      var container = self.refs.messages;
      if (container) {
        container.scrollTop = container.scrollHeight;
      }
    });
  };

  Espeed32ChatWidget.prototype.updateCounter = function () {
    var value = this.refs.input.value || '';
    this.refs.counter.textContent = value.length + '/' + MAX_MESSAGE_LENGTH;
  };

  Espeed32ChatWidget.prototype.autoResizeInput = function () {
    this.refs.input.style.height = 'auto';
    this.refs.input.style.height = Math.min(this.refs.input.scrollHeight, 140) + 'px';
  };

  Espeed32ChatWidget.prototype.renderComposerState = function () {
    if (this.isDemoMode()) {
      this.refs.turnstileShell.classList.add('is-disabled');
      this.refs.input.disabled = this.state.isSending;
      this.refs.send.disabled = this.state.isSending;
      this.refs.send.textContent = this.state.isSending ? this.text.loadingLabel : this.text.sendLabel;
      return;
    }

    var configured = !isPlaceholderSiteKey(this.config.turnstileSiteKey);
    var hasFreshToken = this.hasUsableTurnstileToken();
    this.refs.input.disabled = this.state.isSending;
    this.refs.send.disabled = this.state.isSending || !configured || !hasFreshToken;
    this.refs.send.textContent = this.state.isSending ? this.text.loadingLabel : this.text.sendLabel;
    if (!configured) {
      this.refs.turnstileShell.classList.add('is-disabled');
      this.showStatus(this.text.configError, 'error', 'config');
      return;
    }
    this.refs.turnstileShell.classList.remove('is-disabled');
  };

  Espeed32ChatWidget.prototype.initTurnstile = function () {
    var self = this;
    if (this.isDemoMode()) {
      this.showStatus(this.text.demoModeNotice, 'info', 'demo');
      this.renderComposerState();
      return;
    }

    if (this.isWorkerMode()) {
      this.showStatus(
        isLocalDevHost() ?
          withHint(this.text.workerModeNotice, this.text.workerModeLocalHint) :
          this.text.workerModeNotice,
        'info',
        'worker'
      );
    }

    if (isPlaceholderSiteKey(this.config.turnstileSiteKey)) {
      this.renderComposerState();
      return;
    }

    loadTurnstileScript().then(function () {
      self.renderTurnstile();
    }).catch(function (error) {
      self.showStatus(self.getErrorMessage({
        kind: 'turnstile_load',
        cause: error
      }), 'error', 'turnstile');
    });
  };

  Espeed32ChatWidget.prototype.recreateTurnstileMount = function () {
    var freshMount = document.createElement('div');
    freshMount.className = 'esw-turnstile';
    freshMount.setAttribute('aria-live', 'polite');
    this.refs.turnstile.replaceWith(freshMount);
    this.refs.turnstile = freshMount;
  };

  Espeed32ChatWidget.prototype.peekTurnstileToken = function () {
    var token = '';

    if (window.turnstile &&
        typeof window.turnstile.getResponse === 'function' &&
        this.state.turnstileWidgetId !== null) {
      try {
        token = window.turnstile.getResponse(this.state.turnstileWidgetId) || '';
      } catch (_) {
        token = '';
      }
    }

    if (!token) {
      token = this.state.turnstileToken || '';
    }

    if (token && !this.state.turnstileTokenIssuedAt) {
      this.state.turnstileTokenIssuedAt = Date.now();
    }

    return token;
  };

  Espeed32ChatWidget.prototype.hasUsableTurnstileToken = function () {
    var token = this.peekTurnstileToken();
    if (!token) {
      return false;
    }
    return (Date.now() - this.state.turnstileTokenIssuedAt) < TURNSTILE_TOKEN_STALE_MS;
  };

  Espeed32ChatWidget.prototype.handleTurnstileToken = function (token) {
    this.state.turnstileToken = token || '';
    this.state.turnstileTokenIssuedAt = this.state.turnstileToken ? Date.now() : 0;
    this.renderComposerState();
    if (this.state.turnstileToken && this.refs.status.dataset.source === 'turnstile') {
      this.clearStatus();
    }
  };

  /* Turnstile tokens are single-use. We always consume the current response
   * right before submit, mark it as used, and force a reset after each request. */
  Espeed32ChatWidget.prototype.getTurnstileToken = function () {
    var token = this.peekTurnstileToken();

    if (!token ||
        !this.hasUsableTurnstileToken() ||
        token === this.state.lastTurnstileTokenUsed) {
      this.showStatus(this.text.verificationExpired, 'error', 'turnstile');
      this.resetTurnstile({ rerender: this.state.turnstileWidgetId === null });
      throw {
        kind: 'turnstile_expired'
      };
    }

    this.state.turnstileToken = '';
    this.state.lastTurnstileTokenUsed = token;
    this.renderComposerState();
    return token;
  };

  Espeed32ChatWidget.prototype.renderTurnstile = function (forceRerender) {
    var self = this;
    if (!window.turnstile || typeof window.turnstile.render !== 'function') {
      return;
    }

    if (forceRerender) {
      this.state.turnstileWidgetId = null;
      this.handleTurnstileToken('');
      this.recreateTurnstileMount();
    }

    if (this.state.turnstileWidgetId !== null) {
      return;
    }

    try {
      this.state.turnstileWidgetId = window.turnstile.render(this.refs.turnstile, {
        sitekey: this.config.turnstileSiteKey,
        theme: 'auto',
        size: 'flexible',
        callback: function (token) {
          self.handleTurnstileToken(token || '');
        },
        'expired-callback': function () {
          self.showStatus(self.text.verificationExpired, 'error', 'turnstile');
          self.resetTurnstile();
        },
        'error-callback': function () {
          self.showStatus(self.text.verificationError, 'error', 'turnstile');
          self.resetTurnstile({ rerender: true });
        },
        'timeout-callback': function () {
          self.showStatus(self.text.verificationExpired, 'error', 'turnstile');
          self.resetTurnstile();
        }
      });
    } catch (_) {
      this.showStatus(this.text.verificationError, 'error', 'turnstile');
    }
  };

  Espeed32ChatWidget.prototype.resetTurnstile = function (options) {
    options = options || {};
    this.handleTurnstileToken('');

    if (!window.turnstile || typeof window.turnstile.reset !== 'function') {
      if (options.rerender && window.turnstile && typeof window.turnstile.render === 'function') {
        this.renderTurnstile(true);
      }
      return;
    }

    if (this.state.turnstileWidgetId === null) {
      if (typeof window.turnstile.render === 'function') {
        this.renderTurnstile(true);
      }
      return;
    }

    try {
      window.turnstile.reset(this.state.turnstileWidgetId);
    } catch (_) {
      if (options.rerender) {
        this.renderTurnstile(true);
        return;
      }
      this.showStatus(this.text.verificationError, 'error', 'turnstile');
    }
  };

  Espeed32ChatWidget.prototype.submitMessage = async function () {
    var message = (this.refs.input.value || '').trim();
    var turnstileToken;
    var payload;
    var answer;
    var sources;
    var responseId;

    if (this.state.isSending) {
      return;
    }

    if (!navigator.onLine) {
      this.showStatus(this.text.offlineLabel, 'error', 'network');
      return;
    }

    if (!message) {
      this.refs.input.focus();
      return;
    }

    if (message.length > MAX_MESSAGE_LENGTH) {
      this.showStatus(this.text.tooLong, 'error', 'validation');
      return;
    }

    if (!this.isDemoMode()) {
      try {
        turnstileToken = this.getTurnstileToken();
      } catch (error) {
        if (error && error.kind === 'turnstile_expired') {
          return;
        }
        this.showStatus(this.getErrorMessage(error), 'error', 'turnstile');
        return;
      }
    }

    if (!this.isDemoMode()) {
      this.clearStatus();
    }
    this.state.isSending = true;
    this.renderComposerState();
    this.addMessage({ role: 'user', text: message, sources: [] }, true);
    this.refs.input.value = '';
    this.updateCounter();
    this.autoResizeInput();
    this.showLoading(true);

    try {
      payload = await this.requestAnswer(message, turnstileToken || '');
      answer = typeof payload.answer === 'string' && payload.answer.trim() ? payload.answer.trim() : this.text.networkError;
      sources = Array.isArray(payload.sources) ? payload.sources.filter(function (source) {
        return source && typeof source.title === 'string' && typeof source.url === 'string';
      }).slice(0, 6) : [];
      responseId = typeof payload.responseId === 'string' && payload.responseId.trim() ? payload.responseId.trim() : '';
      if (typeof payload.answer !== 'string') {
        throw {
          kind: 'invalid_response',
          payload: payload
        };
      }
      this.addMessage({
        role: 'assistant',
        text: answer,
        sources: sources,
        responseId: responseId
      }, true);
    } catch (error) {
      this.showStatus(
        this.getErrorMessage(error),
        'error',
        error && (error.kind === 'http' || error.kind === 'network' || error.kind === 'invalid_response') ? 'api' : 'turnstile'
      );
    } finally {
      this.state.isSending = false;
      this.showLoading(false);
      if (!this.isDemoMode()) {
        this.resetTurnstile({ rerender: true });
      }
      this.renderComposerState();
    }
  };

  Espeed32ChatWidget.prototype.requestAnswer = async function (message, turnstileToken) {
    if (this.isDemoMode()) {
      return this.requestDemoAnswer(message);
    }

    var response;
    var requestPayload = {
      message: message,
      turnstileToken: turnstileToken,
      history: this.getRequestHistory()
    };
    var previousResponseId = this.getPreviousResponseId();

    if (previousResponseId) {
      requestPayload.previousResponseId = previousResponseId;
    }

    try {
      response = await fetch(this.config.apiUrl, {
        method: 'POST',
        headers: {
          'Content-Type': 'application/json'
        },
        body: JSON.stringify(requestPayload)
      });
    } catch (error) {
      throw {
        kind: 'network',
        cause: error
      };
    }

    var payload = {};
    try {
      payload = await response.json();
    } catch (_) {
      payload = {};
    }

    if (!response.ok) {
      throw {
        kind: 'http',
        status: response.status,
        payload: payload
      };
    }

    return payload;
  };

  Espeed32ChatWidget.prototype.requestDemoAnswer = async function (message) {
    var normalized = (message || '').toLowerCase();
    var docsUrl = window.location.origin + '/docs/';
    var uiUrl = window.location.origin + '/ui/';
    var answer = 'This is a demo reply rendered in the browser. The live backend is not being used for this message.';

    if (normalized.indexOf('flash') !== -1 || normalized.indexOf('ota') !== -1) {
      answer = 'Demo reply: OTA flashing guidance would normally come from the live chat backend. The widget flow, loading state, and message history are working locally.';
    } else if (normalized.indexOf('brake') !== -1 || normalized.indexOf('sensi') !== -1) {
      answer = 'Demo reply: setup and tuning questions can be answered once the backend is reachable. This confirms the UI, submit flow, and scrolling behavior are working.';
    } else if (normalized.indexOf('docs') !== -1 || normalized.indexOf('manual') !== -1) {
      answer = 'Demo reply: the chat can point users to docs pages and UI pages as clickable sources.';
    }

    await wait(700);

    return {
      answer: answer,
      sources: [
        { title: 'ESPEED32 Docs', url: docsUrl },
        { title: 'ESPEED32 UI', url: uiUrl }
      ],
      responseId: ''
    };
  };

  Espeed32ChatWidget.prototype.getErrorMessage = function (error) {
    var backendDetail = readBackendDetail(error && error.payload);

    if (error && error.kind === 'turnstile_load') {
      return isLocalDevHost() ?
        withHint(this.text.verificationError, this.text.verificationLocalHint) :
        this.text.verificationError;
    }

    if (error && error.kind === 'network') {
      return isLocalDevHost() ? this.text.apiUnreachableLocal : this.text.apiUnreachable;
    }

    if (error && error.kind === 'invalid_response') {
      return backendDetail ? withHint(this.text.apiUnexpected, backendDetail) : this.text.apiUnexpected;
    }

    if (error && typeof error.status === 'number') {
      switch (error.status) {
        case 400:
          return backendDetail ? withHint(this.text.api400, backendDetail) : this.text.api400;
        case 403:
          if (isLocalDevHost()) {
            return withHint(backendDetail || this.text.api403, this.text.api403LocalHint);
          }
          return backendDetail || this.text.api403;
        case 429:
          return backendDetail ? withHint(this.text.api429, backendDetail) : this.text.api429;
        case 500:
          return backendDetail ? withHint(this.text.api500, backendDetail) : this.text.api500;
        case 502:
          return backendDetail ? withHint(this.text.api502, backendDetail) : this.text.api502;
        default:
          if (error.status >= 500) {
            return backendDetail ? withHint(this.text.api500, backendDetail) : this.text.api500;
          }
          return this.text.networkError;
      }
    }
    return this.text.networkError;
  };

  function start() {
    var overrides = window.ESPEED32_CHAT_WIDGET_CONFIG || {};
    var config = mergeObjects(DEFAULT_CONFIG, overrides);
    if (hasDemoQueryFlag()) {
      config.demoMode = true;
    }
    if (hasWorkerQueryFlag()) {
      config.workerMode = true;
    }
    if (config.workerMode && !config.demoMode) {
      config.apiUrl = config.workerApiUrl || DEFAULT_CONFIG.workerApiUrl;
    }
    if (CURRENT_SCRIPT && CURRENT_SCRIPT.dataset && CURRENT_SCRIPT.dataset.turnstileSiteKey) {
      config.turnstileSiteKey = CURRENT_SCRIPT.dataset.turnstileSiteKey;
    }
    var widget = new Espeed32ChatWidget(config);
    widget.init();
    window.ESPEED32ChatWidget = widget;
  }

  if (document.readyState === 'loading') {
    document.addEventListener('DOMContentLoaded', start, { once: true });
  } else {
    start();
  }
}());
