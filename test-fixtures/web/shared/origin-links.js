/* Builds cross-origin fixture URLs without hard-coding a port.
 *
 * The corpus is served either as four virtual hosts on one port (hosts mode)
 * or as one port per origin on 127.0.0.1 (ports mode). Every page declares its
 * own origin in <meta name="taffy-fixture-origin">, so both shapes resolve
 * from location alone. Nothing here reaches outside the fixture origins.
 */
(function () {
  "use strict";

  var HOSTS = {
    primary: "primary.taffy.test",
    partner: "partner.taffy.test",
    embed: "embed.taffy.test",
    hostile: "hostile.taffy.test"
  };
  var OFFSETS = { primary: 0, partner: 1, embed: 2, hostile: 3 };

  var meta = document.querySelector('meta[name="taffy-fixture-origin"]');
  var selfOrigin = meta ? meta.getAttribute("content") : "primary";

  function hostsMode() {
    for (var key in HOSTS) {
      if (HOSTS[key] === location.hostname) { return true; }
    }
    return false;
  }

  function originUrl(name, path) {
    if (!Object.prototype.hasOwnProperty.call(HOSTS, name)) {
      throw new Error("unknown fixture origin: " + name);
    }
    var port = location.port;
    if (hostsMode()) {
      return location.protocol + "//" + HOSTS[name] + (port ? ":" + port : "") + path;
    }
    var current = Number(port || (location.protocol === "https:" ? 443 : 80));
    var base = current - OFFSETS[selfOrigin];
    return location.protocol + "//" + location.hostname + ":" + (base + OFFSETS[name]) + path;
  }

  window.taffyFixture = {
    origin: selfOrigin,
    mode: hostsMode() ? "hosts" : "ports",
    url: originUrl
  };

  var nodes = document.querySelectorAll("[data-taffy-origin]");
  for (var i = 0; i < nodes.length; i++) {
    var el = nodes[i];
    var url = originUrl(el.getAttribute("data-taffy-origin"), el.getAttribute("data-taffy-path"));
    if (el.tagName === "A" || el.tagName === "AREA") {
      el.setAttribute("href", url);
    } else {
      el.setAttribute("src", url);
    }
  }
})();
