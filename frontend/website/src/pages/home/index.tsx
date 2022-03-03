import React from "react"
import classNames from "classnames"
import { Switch, Route } from "react-router-dom"

import Header from "@app/shared/components/header"
import Footer from "@app/shared/components/footer"
import Hero from "@app/pages/home/hero"
import Benefits from "@app/pages/home/benefits"
import Features from "@app/pages/home/features"
import Users from "@app/pages/home/users"
import Testimonial from "@app/pages/home/testimonial"

const padded = "pb-20 px-12 max-w-screen-2xl m-auto overflow-x-hidden dark"

export const Home = () => {
  /*
        Product page for Chrome

        Arguments: none
    */
  return (
    <>
      <div className={classNames(padded, "bg-gray-900")}>
        <Header />
        <Hero />
        <Features />
        <Testimonial />
      </div>
      <div className={classNames(padded, "bg-blue-darker")}>
        <Users />
      </div>
      <div className={classNames(padded, "bg-gray-900")}>
        <Benefits />
      </div>
      <Footer />
    </>
  )
}

const Router = () => {
  /*
        Sub-router for home page
        Arguments: none
    */

  return (
    <div>
      <Switch>
        <Route exact path="/" component={Home} />
      </Switch>
    </div>
  )
}

export default Router
