import { defineConfig } from "vitepress";

// The notes write placeholders like "<city> Chapter <n>" or "Error <code>.<module>" in plain text. Markdown
// passes them through as HTML, which Vue then rejects as unclosed elements. So inline HTML is kept only
// for real elements that are closed in the same paragraph (or void, like <br> and <img>), and an HTML
// block only when its tag is a real element; anything else is shown as text. The notes need no escaping.
const HTML_TAGS = new Set(
  ("a abbr b br code dd del details div dl dt em figcaption figure h1 h2 h3 h4 h5 h6 hr i img kbd li ol p " +
    "picture pre s section small source span strong style sub summary sup table tbody td th thead tr u ul")
    .split(" "),
);
const VOID_TAGS = new Set(["br", "hr", "img", "source"]);
const tagOf = (html: string) => {
  const m = /^<(\/?)([A-Za-z][\w-]*)[^>]*?(\/?)>$/.exec(html.trim());
  return m ? { close: m[1] === "/", name: m[2].toLowerCase(), self: m[3] === "/" } : null;
};
function htmlAsTextUnlessReal(md: any) {
  md.core.ruler.push("html_as_text", (state: any) => {
    for (const block of state.tokens) {
      if (block.type === "html_block") {
        const t = /^<\/?([A-Za-z][\w-]*)/.exec(block.content.trim());
        if (t && !HTML_TAGS.has(t[1].toLowerCase())) {
          block.type = "paragraph_text";
        }
        continue;
      }
      if (block.type !== "inline" || !block.children) continue;
      const kids = block.children;
      const open: { name: string; i: number }[] = [];
      const bad = new Set<number>();
      kids.forEach((k: any, i: number) => {
        if (k.type !== "html_inline") return;
        const t = tagOf(k.content);
        if (!t || !HTML_TAGS.has(t.name)) return bad.add(i);
        if (t.self || VOID_TAGS.has(t.name)) return;
        if (!t.close) return open.push({ name: t.name, i });
        const j = open.map((o) => o.name).lastIndexOf(t.name);
        if (j < 0) return bad.add(i);
        for (const o of open.splice(j).slice(1)) bad.add(o.i);   // opened inside it, never closed
      });
      for (const o of open) bad.add(o.i);
      for (const i of bad) kids[i].type = "text";
    }
  });
  // An HTML block demoted above: its source as an escaped paragraph.
  md.renderer.rules.paragraph_text = (tokens: any[], idx: number) =>
    `<p>${md.utils.escapeHtml(tokens[idx].content.trim())}</p>\n`;
}

export default defineConfig({
  title: "OpenNFSPU",
  description:
    "A faithful reimplementation of Need for Speed: Porsche Unleashed (Electronic Arts, 2000) in C11 for the gasm runtime, using the data from your own copy of the game.",
  cleanUrls: true,
  lastUpdated: true,
  srcExclude: ["README.md", "AGENTS.md", "CONTEXT.md"],
  // The reverse-engineering notes keep their README.md (it is also what GitHub shows in docs/re/).
  rewrites: { "re/README.md": "re/index.md" },
  sitemap: { hostname: "https://opennfspu.emdzej.pl" },
  // The browser player is a static page in public/play/, not a Markdown page.
  ignoreDeadLinks: [/^\/play\//],
  markdown: { config: htmlAsTextUnlessReal },

  head: [
    ["link", { rel: "icon", href: "/favicon.svg", type: "image/svg+xml" }],
    ["meta", { name: "theme-color", content: "#a3121e" }],
    ["meta", { property: "og:title", content: "OpenNFSPU: Need for Speed: Porsche Unleashed, reimplemented" }],
    [
      "meta",
      {
        property: "og:description",
        content:
          "A faithful reimplementation of Need for Speed: Porsche Unleashed (2000) in C11, ported from the original executable. Runs on gasm, natively and in the browser, with the data from your own copy.",
      },
    ],
    ["meta", { property: "og:url", content: "https://opennfspu.emdzej.pl/" }],
  ],

  themeConfig: {
    siteTitle: "OpenNFSPU",
    logo: "/favicon.svg",

    nav: [
      { text: "Guide", link: "/guide/", activeMatch: "/guide/" },
      { text: "How-tos", link: "/howto/build-from-source", activeMatch: "/howto/" },
      { text: "Reverse engineering", link: "/re/", activeMatch: "/(re|formats)" },
      // A static app in docs/public/play/: target _self makes it a full page load, not a VitePress route.
      { text: "Play", link: "/play/", target: "_self" },
    ],

    sidebar: {
      "/guide/": [
        {
          text: "Guide",
          items: [
            { text: "Introduction", link: "/guide/" },
            { text: "Status", link: "/guide/status" },
            { text: "Installing", link: "/guide/install" },
            { text: "Game data", link: "/guide/game-data" },
            { text: "Running on gasm", link: "/guide/gasm" },
            { text: "Playing in the browser", link: "/guide/playing" },
            { text: "Controls", link: "/guide/controls" },
          ],
        },
      ],
      "/howto/": [
        {
          text: "How-tos",
          items: [
            { text: "Build from source", link: "/howto/build-from-source" },
            { text: "Extract the game", link: "/howto/extract-game" },
            { text: "Make a release", link: "/howto/release" },
          ],
        },
      ],
      "/": [
        {
          text: "Reverse engineering",
          items: [
            { text: "Overview", link: "/re/" },
            { text: "Architecture", link: "/re/architecture" },
            { text: "Inventory", link: "/re/inventory" },
            { text: "Port plan", link: "/re/port-plan" },
            { text: "THRASH driver", link: "/re/thrash" },
            { text: "EAGL2", link: "/re/eagl" },
            { text: "Track and world", link: "/re/track" },
            { text: "Drawing the track", link: "/re/track-render" },
            { text: "Scene elements", link: "/re/scene" },
            { text: "Sky", link: "/re/sky" },
            { text: "Cars", link: "/re/cars" },
            { text: "Sound", link: "/re/sound" },
            { text: "Textures and rendering", link: "/re/render" },
            { text: "Render system", link: "/re/render-system" },
            { text: "Front end", link: "/re/frontend" },
            { text: "File formats", link: "/formats" },
          ],
        },
        {
          text: "Internals",
          items: [{ text: "Porting conventions", link: "/internals/porting" }],
        },
      ],
    },

    socialLinks: [{ icon: "github", link: "https://github.com/emdzej/opennfspu" }],

    editLink: {
      pattern: "https://github.com/emdzej/opennfspu/edit/main/docs/:path",
      text: "Edit this page on GitHub",
    },

    outline: { level: [2, 3] },

    search: { provider: "local" },

    footer: {
      message:
        "OpenNFSPU is released under the GPL-3.0. Need for Speed: Porsche Unleashed is © 2000 Electronic Arts; Porsche is a trademark of Dr. Ing. h.c. F. Porsche AG. OpenNFSPU contains no original code or assets and is not affiliated with them: it runs the game from your own copy. It runs on <a href=\"https://gasm.emdzej.pl\">gasm</a>.",
    },
  },
});
