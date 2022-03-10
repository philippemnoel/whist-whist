import React from "react"

const Testimonial = () => {
  return (
    <section className="mt-20">
      <div className="max-w-7xl w-full mx-auto grid md:grid-cols-2 gap-y-6 lg:gap-y-0">
        <div className="bg-blue py-12 px-16 md:flex md:flex-col md:py-16 md:mr-3 rounded">
          <blockquote className="mt-6 md:flex-grow md:flex md:flex-col">
            <div className="relative text-lg font-medium text-gray-900 md:flex-grow">
              <svg
                className="absolute top-0 left-0 transform -translate-x-3 -translate-y-2 h-8 w-8 text-blue-light"
                fill="currentColor"
                viewBox="0 0 32 32"
                aria-hidden="true"
              >
                <path d="M9.352 4C4.456 7.456 1 13.12 1 19.36c0 5.088 3.072 8.064 6.624 8.064 3.36 0 5.856-2.688 5.856-5.856 0-3.168-2.208-5.472-5.088-5.472-.576 0-1.344.096-1.536.192.48-3.264 3.552-7.104 6.624-9.024L9.352 4zm16.512 0c-4.8 3.456-8.256 9.12-8.256 15.36 0 5.088 3.072 8.064 6.624 8.064 3.264 0 5.856-2.688 5.856-5.856 0-3.168-2.304-5.472-5.184-5.472-.576 0-1.248.096-1.44.192.48-3.264 3.456-7.104 6.528-9.024L25.864 4z" />
              </svg>
              <p className="relative">
                Whist makes it possible for me to run Chrome and Visual Studio
                Code at the same time on my old Macbook Pro. My computer&lsquo;s
                performance before and after Whist is striking.
              </p>
            </div>
            <footer className="mt-6">
              <div className="flex items-start">
                <div>
                  <div className="text-base font-semibold text-gray-900">
                    Judith P.
                  </div>
                  <div className="text-base font-medium text-gray-800">
                    Software Developer
                  </div>
                </div>
              </div>
            </footer>
          </blockquote>
        </div>
        <div className="bg-blue py-12 px-16 md:flex md:flex-col md:py-16 md:ml-3 rounded">
          <blockquote className="mt-6 md:flex-grow md:flex md:flex-col">
            <div className="relative text-lg font-medium text-gray-900 md:flex-grow">
              <svg
                className="absolute top-0 left-0 transform -translate-x-3 -translate-y-2 h-8 w-8 text-blue-light"
                fill="currentColor"
                viewBox="0 0 32 32"
              >
                <path d="M9.352 4C4.456 7.456 1 13.12 1 19.36c0 5.088 3.072 8.064 6.624 8.064 3.36 0 5.856-2.688 5.856-5.856 0-3.168-2.208-5.472-5.088-5.472-.576 0-1.344.096-1.536.192.48-3.264 3.552-7.104 6.624-9.024L9.352 4zm16.512 0c-4.8 3.456-8.256 9.12-8.256 15.36 0 5.088 3.072 8.064 6.624 8.064 3.264 0 5.856-2.688 5.856-5.856 0-3.168-2.304-5.472-5.184-5.472-.576 0-1.248.096-1.44.192.48-3.264 3.456-7.104 6.528-9.024L25.864 4z" />
              </svg>
              <p className="relative">
                Comparing Chrome to Whist is like comparing night and day. Even
                with the latest Macbook, I notice that pages load painfully
                slower when I use Chrome.
              </p>
            </div>
            <footer className="mt-6">
              <div className="flex items-start">
                <div>
                  <div className="text-base font-semibold text-gray-900">
                    Jose L.
                  </div>
                  <div className="text-base font-medium text-gray-800">
                    Product Manager
                  </div>
                </div>
              </div>
            </footer>
          </blockquote>
        </div>
      </div>
    </section>
  )
}

export default Testimonial
