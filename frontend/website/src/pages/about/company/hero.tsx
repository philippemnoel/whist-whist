import React from "react"

const Hero = () => (
  <div>
    <div className="max-w-7xl mx-auto py-16 px-4 sm:py-24 sm:px-6 lg:px-8 mt-16">
      <div className="text-center">
        <div className="text-base font-semibold text-gray-500 tracking-wide uppercase mb-4">
          Mission
        </div>
        <p className="max-w-xl m-auto text-4xl text-gray-300 sm:text-5xl sm:tracking-tight lg:text-6xl">
          Performant hardware for everyone.
        </p>
        <p className="max-w-xl mt-5 mx-auto text-xl text-gray-400">
          We spend most of our time in the browser, yet our browsers are not
          equipped to handle the heaviest web apps. Large Google Slides lag.
          Massive Figma files take forever to load. Your fans spin when you open
          too many tabs.
        </p>
        <p className="max-w-xl mt-5 mx-auto text-xl text-gray-400">
          Whist makes your workflow faster by moving your computer&lsquo;s most
          draining application — Google Chrome — to the cloud.
        </p>
      </div>
    </div>
  </div>
)

export default Hero
