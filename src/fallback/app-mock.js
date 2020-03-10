(function maybeFakeGreeterContext() {
  // If we're not inside the greeter context, this fakes the injected objects,
  // part of Qt's QWebChannel framework. It's comprised of the QWebChannel
  // class that is used to register the channel handler (our logic), and the
  // transport, qt.webChannelTransport.
  if (window.QWebChannel !== undefined && window.qt !== undefined) return;

  console.warn("Outside Qt greeter. Mocking channel and transport.");

  const fakeSignal = () => ({
    connect: function (cb) {
      this.cb = cb;
    },
    disconnect: function () {
    },
    notify: function () {
      this.cb.apply(this, arguments);
    },
  });

  const prologin = {
    // Shims of the greeter ProloginJs object that just log the calls.
    Authenticate: function () {
      const that = this;
      return new Promise(function (accept, reject) {
        console.log(`called WebAuthenticate(${arguments})`);
        that.OnStatusMessage.notify("fake authenticating…");
        setTimeout(() => that.OnStatusMessage.notify("please wait a bit…"), 1000);
        setTimeout(() => that.OnStatusMessage.notify("real soon now…"), 2000);
        // setTimeout(() => that.OnLoginSuccess.notify(), 3000);
        setTimeout(() => that.OnLoginError.notify("BAD PASSWORD"), 3000);
        setTimeout(() => reject(), 3100);
      });
    },
    AvailableSessions: function () {
      console.log("called AvailableSessions()");
      return Promise.resolve([
        {id: "kde", name: "KDE", description: "This is KDE"},
        {id: "gnome", name: "Gnome", description: "This is Gnome"},
        {id: "i3", name: "i3 wm", description: "This is i3"},
        {id: "xfce", name: "XFCE", description: "This is XFCE"},
      ]);
    },
    SetLanguage: function (l) {
      alert(`Setting language to ${l}`)
    },
    PowerOff: function () {
      alert("Powering off")
    },
    Reboot: function () {
      alert("Rebooting")
    },
    OnStatusMessage: fakeSignal(),
    OnLoginSuccess: fakeSignal(),
    OnLoginError: fakeSignal(),
  };

  window.QWebChannel = function (transport, callback) {
    callback({objects: {prologin}});
  };

  window.qt = {webChannelTransport: null};
})();