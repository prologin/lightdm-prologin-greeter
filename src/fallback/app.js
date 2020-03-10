function invalidShake($el) {
  $el.classList.add("invalid");
  setTimeout(() => $el.classList.remove("invalid"), 750);
}

(function () {
  const $form = document.getElementById('form');
  const $status = document.getElementById('text-status');
  const $username = document.getElementById('input-username');
  const $password = document.getElementById('input-password');
  const $login = document.getElementById('btn-login');
  const $sessions = document.getElementById('sessions');
  const $poweroff = document.getElementById('btn-poweroff');
  const $reboot = document.getElementById('btn-reboot');
  const interactiveElements = [$username, $password, $login];

  $username.focus();

  let prologin;
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

  function createSessionRadios(sessions) {
    sessions.forEach(s => {
      const $radio = document.createElement("input");
      $radio.type = "radio";
      $radio.name = "session";
      $radio.value = s.id;
      $radio.id = `session-${s.id}`;
      const $label = document.createElement("label");
      $label.htmlFor = $radio.id;
      const $s = document.createElement("span");
      $s.title = s.description;
      $s.textContent = s.name;
      $label.appendChild($radio);
      $label.appendChild($s);
      $sessions.appendChild($label);
      interactiveElements.push($radio);
    });
    // Select first session by default.
    $sessions.querySelector('input[type=radio]').checked = true;
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
    interactiveElements.forEach(e => e.disabled = false);
    $password.focus();
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
    interactiveElements.forEach(e => e.disabled = true);

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
    prologin.OnStatusMessage.connect(onStatusMessage);
    prologin.OnLoginSuccess.connect(onLoginSuccess);
    prologin.OnLoginError.connect(onLoginError);
  });

})();
