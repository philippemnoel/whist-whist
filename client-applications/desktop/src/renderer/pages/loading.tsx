import React from "react"

const Loading = () => (
  <div className="bg-white bg-opacity-90 rounded w-screen h-screen">
    <div className="font-body text-center flex space-x-6 justify-center pt-5">
      <div>
        <svg
          className="animate-spin h-5 w-5 text-gray"
          xmlns="http://www.w3.org/2000/svg"
          fill="none"
          viewBox="0 0 24 24"
        >
          <circle
            className="opacity-25"
            cx="12"
            cy="12"
            r="10"
            stroke="currentColor"
            stroke-width="4"
          ></circle>
          <path
            className="opacity-75"
            fill="currentColor"
            d="M4 12a8 8 0 018-8V0C5.373 0 0 5.373 0 12h4zm2 5.291A7.962 7.962 0 014 12H0c0 3.042 1.135 5.824 3 7.938l3-2.647z"
          ></path>
        </svg>
      </div>
      <div className="text-gray font-medium">Fractal is launching</div>
    </div>
  </div>
)

export default Loading
