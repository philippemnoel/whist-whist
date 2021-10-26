import React, { useState } from "react"
import classNames from "classnames"

import { FractalButton, FractalButtonState } from "@app/components/html/button"

const SelectBrowser = (props: {
  browsers: string[]
  onSelect: (browser: string) => void
}) => {
  return (
    <div className="w-full">
      <div className="font-semibold text-2xl text-gray-300">
        Sync your cookies and passwords
      </div>
      <div className="mt-2 mb-4 text-gray-500">
        Which of your current browsers would you like to sync with?
      </div>
      <div className="mt-6">
        <div className="bg-gray-800 px-4 py-2 rounded w-72 m-auto">
          <select
            id="browser"
            name="browser"
            className="bg-gray-800 font-semibold text-gray-300 outline-none mt-1 block w-full py-2 text-base border-gray-300 sm:text-sm"
            defaultValue="Canada"
            onChange={(evt: any) => props.onSelect(evt.target.value)}
          >
            {props.browsers.map((browser: string, index: number) => (
              <option key={index}>{browser}</option>
            ))}
            <option value={undefined}>None, start from a clean slate</option>
          </select>
        </div>
      </div>
    </div>
  )
}

const Importer = (props: {
  browsers: string[]
  onSubmit: (browser: string) => void
}) => {
  const [browser, setBrowser] = useState<string | undefined>(undefined)
  const [processing, setProcessing] = useState(false)

  const onSubmit = (browser: string) => {
    setProcessing(true)
    props.onSubmit(browser)
  }

  const onSelect = (browser: string) => {
    setBrowser(browser)
  }

  return (
    <div
      className={classNames(
        "flex flex-col h-screen items-center bg-black bg-opacity-90",
        "justify-center font-body text-center px-12"
      )}
    >
      <SelectBrowser
        browsers={props.browsers}
        onSelect={(browser: string) => onSelect(browser)}
      />
      <div>
        <FractalButton
          contents="Continue"
          className="mt-4 px-12 w-72 mx-auto py-2 text-gray-300 hover:text-black"
          state={
            processing
              ? FractalButtonState.PROCESSING
              : FractalButtonState.DEFAULT
          }
          onClick={() => onSubmit(browser)}
        />
      </div>
    </div>
  )
}

export default Importer
