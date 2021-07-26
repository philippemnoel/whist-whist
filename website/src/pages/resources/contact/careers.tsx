import React from "react"

const Careers = () => (
    <>
        <div
            className="hidden absolute inset-x-0 h-1/2 bg-blue-gray-50 lg:block"
            aria-hidden="true"
        />
        <div className="max-w-7xl mx-auto bg-blue-600 lg:bg-transparent lg:px-8 mt-36 mb-36">
            <div className="lg:grid lg:grid-cols-12">
                <div className="relative z-10 lg:col-start-1 lg:row-start-1 lg:col-span-4 lg:py-8 lg:bg-transparent">
                    <div
                        className="absolute inset-x-0 h-1/2 bg-blue lg:hidden"
                        aria-hidden="true"
                    />
                    <div className="max-w-md mx-auto px-4 sm:max-w-3xl sm:px-6 lg:max-w-none lg:p-0">
                        <div className="aspect-w-10 aspect-h-6 sm:aspect-w-2 sm:aspect-h-1 lg:aspect-w-1">
                            <img
                                className="object-cover object-center rounded-3xl"
                                src="https://images.unsplash.com/photo-1507207611509-ec012433ff52?ixlib=rb-1.2.1&ixid=MXwxMjA3fDB8MHxwaG90by1wYWdlfHx8fGVufDB8fHw%3D&auto=format&fit=crop&w=934&q=80"
                                alt=""
                            />
                        </div>
                    </div>
                </div>

                <div className="relative bg-blue lg:col-start-3 lg:row-start-1 lg:col-span-10 lg:rounded-3xl lg:grid lg:grid-cols-10 lg:items-center">
                    <div className="relative max-w-md mx-auto py-12 px-4 space-y-6 sm:max-w-3xl sm:py-16 sm:px-6 lg:max-w-none lg:p-0 lg:col-start-4 lg:col-span-6">
                        <h2
                            className="text-3xl font-extrabold text-white"
                            id="join-heading"
                        >
                            Join our team
                        </h2>
                        <p className="text-lg text-gray-200">
                            If working on a product that pushes the boundaries
                            of network protocols, virtualization, or browser
                            OSes excites you, you can apply to work with us!
                        </p>
                        <a
                            className="no-underline block w-full py-3 px-5 text-center bg-white border border-transparent rounded text-base font-medium text-gray sm:inline-block sm:w-auto"
                            href="https://www.notion.so/tryfractal/Fractal-Job-Board-a39b64712f094c7785f588053fc283a9"
                            target="_blank"
                            rel="noreferrer"
                        >
                            Explore open positions
                        </a>
                    </div>
                </div>
            </div>
        </div>
    </>
)

export default Careers
