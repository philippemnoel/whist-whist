import React, { useState, useEffect, useRef } from "react"
import FuzzySearch from "fuzzy-search"
import range from "lodash.range"

import Search from "@app/renderer/pages/omnibar/search"
import Option from "@app/renderer/pages/omnibar/option"

import { useMainState } from "@app/renderer/utils/ipc"
import { withContext } from "@app/renderer/context/omnibar"
import { createOptions } from "@app/renderer/utils/omnibar"

const Omnibar = () => {
  const [activeIndex, setActiveIndex] = useState(0)
  const [mainState, setMainState] = useMainState()
  const [options, setOptions] = useState(createOptions(mainState, setMainState))
  const context = withContext()

  const refs = range(0, options.length).map(() => useRef(null))

  const onKeyDown = (e: any) => {
    if (e.key === "Enter") options[activeIndex].onClick()
    if (e.key === "ArrowDown") {
      if ((activeIndex + 1) % 4 === 0 && activeIndex < options.length - 1) {
        refs[activeIndex + 1].current.scrollIntoView({ block: "start" })
      }
      setActiveIndex(Math.min(activeIndex + 1, options.length - 1))
    }
    if (e.key === "ArrowUp") {
      if (
        (activeIndex + 2) % 4 === 0 &&
        activeIndex < options.length - 1 &&
        activeIndex > 1
      ) {
        refs[activeIndex - 1].current.scrollIntoView({ block: "end" })
      }
      setActiveIndex(Math.max(0, activeIndex - 1))
    }
  }

  useEffect(() => {
    const searcher = new FuzzySearch(
      createOptions(mainState, setMainState),
      ["text", "keywords"],
      {
        caseSensitive: false,
      }
    )
    setOptions(searcher.search(context.search))
  }, [context.search, mainState])

  return (
    <div
      onKeyDown={onKeyDown}
      tabIndex={0}
      className="w-screen h-screen text-gray-100 text-center bg-gray-800 p-3 relative font-body focus:outline-none py-6 px-8 overflow-hidden"
    >
      <div>
        <Search />
      </div>
      <div className="overflow-y-scroll h-56 pb-12">
        {options.map((option) => (
          <div
            onMouseEnter={() => setActiveIndex(option.id)}
            key={option.id}
            ref={refs[option.id]}
            className="duration-100"
          >
            <Option
              icon={option.icon()}
              text={option.text}
              active={activeIndex === option.id}
              onClick={option.onClick}
              rightElement={option?.rightElement}
            />
          </div>
        ))}
      </div>
    </div>
  )
}

export default Omnibar
