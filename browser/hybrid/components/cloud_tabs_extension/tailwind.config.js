module.exports = {
  // This "content" list should reference any files that call our .css files by
  // name. This includes html, and any js files that 'import' css.
  // This will allow tailwind to analyze and "purge" unused css.
  content: ["./src/**/*.{js,jsx,ts,tsx}", "./src/tabs/index.html"],
  darkMode: "class",
  // The theme section is where we define anything related to visual design.
  theme: {
    // The extend sections lets us add extra "variants" to utilities. We also
    // override some of the default utilities.
    extend: {
      colors: {
        blue: {
          DEFAULT: "#0092FF",
          light: "#71D9FF",
          dark: "#0000CE",
        },
        gray: {
          800: "#313341",
          900: "#1e2029"
        }
      },
    },
  },
}
