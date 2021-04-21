/** @type {import("snowpack").SnowpackUserConfig } */
module.exports = {
  mount: {
    public: { url: "/", static: true },
    src: { url: "/dist" },
  },
  alias: {
    "@app/assets": "./public/assets",
    "@app": "./src",
  },
  plugins: [
    "@snowpack/plugin-react-refresh",
    "@snowpack/plugin-dotenv",
    "@snowpack/plugin-typescript",
  ],
  routes: [{ match: "routes", src: ".*", dest: "/index.html" }],
};
