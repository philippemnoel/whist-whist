import React, { useContext } from "react"
import { Row, Col } from "react-bootstrap"

import Testimonial from "pages/landing/components/testimonial"
import DemoVideos from "pages/landing/components/demoVideos"

import SideBySide from "shared/components/sideBySide"
import { ScreenSize } from "shared/constants/screenSizes"
import MainContext from "shared/context/mainContext"

import "styles/landing.css"

const MiddleView = (props: any) => {
    const { width } = useContext(MainContext)

    return (
        <div style={{ marginTop: width >= ScreenSize.LARGE ? 100 : 0 }}>
            <div style={{ position: "relative", width: "100%" }}>
                <SideBySide case={"Productivity"} />
            </div>
            <DemoVideos />
            <Row
                style={{
                    marginTop: width >= ScreenSize.LARGE ? 175 : 75,
                }}
            >
                <Col
                    lg={4}
                    style={{
                        position: "relative",
                        bottom: width >= ScreenSize.LARGE ? 80 : 0,
                        paddingRight: width >= ScreenSize.LARGE ? 30 : 15,
                    }}
                >
                    <Testimonial
                        invert
                        text="Woah! What you've developed is something I've been dreaming of for years now... this is a MASSIVE game changer for people that do the kind of work that I do."
                        name="Bert K."
                        job="Animator + Graphics Designer"
                    />
                </Col>
                <Col
                    lg={4}
                    style={{
                        position: "relative",
                        paddingLeft: 15,
                        paddingRight: 15,
                    }}
                >
                    <Testimonial
                        title="Fractal helps Jonathan run Google Chrome with less RAM."
                        text="[Fractal] is an immense project with a fantastic amount of potential. Now my computer doesn't slow down when I have too many apps open."
                        name="Christina I."
                        job="Software Developer"
                    />
                </Col>
                <Col
                    lg={4}
                    style={{
                        position: "relative",
                        paddingLeft: width >= ScreenSize.LARGE ? 30 : 15,
                        top: width >= ScreenSize.LARGE ? 80 : 0,
                    }}
                >
                    <Testimonial
                        invert
                        text="AMAZING ... tried Figma - it was buttery smooth with almost no detectable latency. Never thought it would work this well."
                        name="Ananya P."
                        job="Graphics Designer"
                    />
                </Col>
            </Row>
            <Row
                style={{ marginTop: width >= ScreenSize.LARGE ? 125 : 50 }}
                id="leaderboard"
            >
                <Col md={12} lg={9}>
                    <h1>How can I get access?</h1>
                    <p style={{ marginTop: 30 }}>
                        Join the waitlist—when the countdown hits zero, we’ll
                        invite the top 100 people on the leaderboard to try
                        Fractal, which you can climb by referring friends. We'll
                        also invite an additional 20 people with the most
                        compelling 100-word submission on why they want Fractal.
                    </p>
                </Col>
            </Row>
        </div>
    )
}

export default MiddleView
