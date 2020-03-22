function invalidShake($el) {
  $el.classList.add("invalid");
  setTimeout(() => $el.classList.remove("invalid"), 750);
}

let prologin;
(function () {
  const $form = document.getElementById('form');
  const $status = document.getElementById('text-status');
  const $username = document.getElementById('input-username');
  const $password = document.getElementById('input-password');
  const $capslock = document.getElementById('capslock');
  const $numlock = document.getElementById('numlock');
  const $login = document.getElementById('btn-login');
  const $sessions = document.getElementById('sessions');
  const $layouts = document.getElementById('layouts');
  const $poweroff = document.getElementById('btn-poweroff');
  const $reboot = document.getElementById('btn-reboot');

  const $interactiveElements = [$username, $password, $login];
  const $indicators = [$capslock, $numlock];

  $username.focus();
  let statusTimeout;

  $poweroff.onclick = (e) => {
    e.preventDefault();
    if (confirm("Do you really want to power off?")) {
      prologin.PowerOff();
    }
  };

  $reboot.onclick = (e) => {
    e.preventDefault();
    if (confirm("Do you really want to reboot?")) {
      prologin.Reboot();
    }
  };

  function createRadios(name, $elem, iterable, idFunc, labelFunc, titleFunc, onChangeFunc) {
    $elem.innerHTML = '';
    iterable.forEach((item, i) => {
      const $radio = document.createElement("input");
      $radio.type = "radio";
      $radio.name = name;
      $radio.value = idFunc(item, i);
      $radio.id = `${name}-${$radio.value}`;
      const $label = document.createElement("label");
      $label.htmlFor = $radio.id;
      const $s = document.createElement("span");
      $s.title = titleFunc(item, i);
      $s.textContent = labelFunc(item, i);
      $label.appendChild($radio);
      $label.appendChild($s);
      $elem.appendChild($label);
      $interactiveElements.push($radio);
      if (onChangeFunc) {
        $radio.addEventListener('change', onChangeFunc);
      }
    });
    // Select first by default.
    $elem.querySelector('input[type=radio]').checked = true;
  }

  function createSessionRadios(sessions) {
    createRadios('sessions', $sessions, sessions,
      s => s.id, s => s.name,
      s => `Log-in using window manager: ${s.description}`);
  }

  function createLayoutRadios(layouts) {
    createRadios('layouts', $layouts, layouts,
      (s, i) => i,
      s => s.long,
      s => `Keyboard layout: ${s.short}`,
      async function () {
        const id = this.value;
        await prologin.SetKeyboardLayout(id);
      });
  }

  function setStatus(message, error) {
    $status.classList.toggle("error", error);
    $status.textContent = message;
    clearInterval(statusTimeout);
    statusTimeout = setTimeout(() => {
      $status.innerHTML = "&nbsp;";
      $status.classList.toggle("error", false);
    }, 8000);
  }

  function onStatusMessage(message) {
    setStatus(message, false);
  }

  function onLoginSuccess() {
    setStatus("Login successful, launching your session…", false);
  }

  function onLoginError(reason) {
    setStatus(`Login error: ${reason}`, true);

    // Reset form.
    $interactiveElements.forEach(e => e.disabled = false);
    $indicators.forEach(e => e.classList.remove('disabled'));
    $password.focus();
  }

  async function onKeyboardLayoutsChange() {
    createLayoutRadios(await prologin.KeyboardLayouts());
  }

  function onKeyboardLayoutChange(id) {
    document.querySelector(`#layouts-${id}`).checked = true;
  }

  function onCapsLockChange(enabled) {
    $capslock.classList.toggle('visible', enabled);
  }

  function onNumLockChange(enabled) {
    $numlock.classList.toggle('visible', enabled);
  }

  $form.onsubmit = async function (e) {
    e.preventDefault();

    if (!$username.value.trim().length) {
      $username.focus();
      invalidShake($username);
      return;
    }

    if (!$password.value.trim().length) {
      $password.focus();
      invalidShake($password);
      return;
    }

    const session = $sessions
      .querySelector("input[type=radio]:checked").value;

    // Disable form, authenticate.
    $interactiveElements.forEach(e => e.disabled = true);
    $indicators.forEach(e => e.classList.add('disabled'));

    setStatus("starting authentication…", false);
    try {
      await prologin.Authenticate($username.value, $password.value, session);
    } catch (e) {
    }
    $password.value = "";
  };

  new QWebChannel(qt.webChannelTransport, async function (channel) {
    prologin = channel.objects.prologin;
    createSessionRadios(await prologin.AvailableSessions());
    createLayoutRadios(await prologin.KeyboardLayouts());
    prologin.OnStatusMessage.connect(onStatusMessage);
    prologin.OnLoginSuccess.connect(onLoginSuccess);
    prologin.OnLoginError.connect(onLoginError);
    prologin.OnCapsLockChange.connect(onCapsLockChange);
    prologin.OnNumLockChange.connect(onNumLockChange);
    prologin.OnKeyboardLayoutsChange.connect(onKeyboardLayoutsChange);
    prologin.OnKeyboardLayoutChange.connect(onKeyboardLayoutChange);
  });

})();
