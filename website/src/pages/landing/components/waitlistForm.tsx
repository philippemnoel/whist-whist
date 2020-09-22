import React, { useState, useEffect, useContext } from "react"
import { connect } from "react-redux"
import { Button } from "react-bootstrap"
import { CountryDropdown } from "react-country-region-selector"

import history from "shared/utils/history"
import { db } from "shared/utils/firebase"
import ScreenContext from "shared/context/screenContext"
import { INITIAL_POINTS } from "shared/utils/points"
import { insertWaitlistAction } from "store/actions/auth/waitlist"

import "styles/landing.css"

const WaitlistForm = (props: any) => {
    const { dispatch, user } = props
    const { width } = useContext(ScreenContext)

    const [email, setEmail] = useState("")
    const [name, setName] = useState("")
    const [country, setCountry] = useState("United States")
    const [processing, setProcessing] = useState(false)
    const [, setReferralCode] = useState()

    useEffect(() => {
        console.log("Use Effect waitlist")
    }, [])

    const updateEmail = (evt: any) => {
        evt.persist()
        setEmail(evt.target.value)
    }

    const updateName = (evt: any) => {
        evt.persist()
        setName(evt.target.value)
    }

    const updateCountry = (country: string) => {
        setCountry(country)
    }

    const updateReferralCode = (evt: any) => {
        evt.persist()
        setReferralCode(evt.target.value)
    }

    const insertWaitlist = async () => {
        setProcessing(true)

        var emails = db.collection("waitlist").where("email", "==", email)
        const exists = await emails.get().then((snapshot: any) => {
            return !snapshot.empty
        })

        if (!exists) {
            db.collection("waitlist")
                .doc(email)
                .set({
                    name: name,
                    email: email,
                    referrals: 0,
                    points: INITIAL_POINTS,
                    google_auth_email: "",
                })
                .then(() =>
                    dispatch(
                        insertWaitlistAction(email, name, INITIAL_POINTS, 0)
                    )
                )
                .then(() => history.push("/application"))
        } else {
            db.collection("waitlist")
                .doc(email)
                .get()
                .then((snapshot) => {
                    let document = snapshot.data()
                    if (document) {
                        dispatch(
                            insertWaitlistAction(
                                email,
                                document.name,
                                document.points,
                                0
                            )
                        )
                    }
                })
                .then(() => setProcessing(false))
        }
    }

    return (
        <div style={{ zIndex: 100 }}>
            <div
                style={{
                    width: 800,
                    maxWidth: "100%",
                    margin: "auto",
                    marginTop: 20,
                    marginBottom: 0,
                    display: width > 720 ? "flex" : "block",
                    justifyContent: "space-between",
                    zIndex: 100,
                    padding: width > 720 ? 0 : 30,
                    paddingBottom: 0,
                }}
            >
                <input
                    type="text"
                    placeholder="Email Address"
                    onChange={updateEmail}
                    className="waitlist-form"
                    style={{ width: width > 720 ? 180 : "100%" }}
                />
                <input
                    type="text"
                    placeholder="Name"
                    onChange={updateName}
                    className="waitlist-form"
                    style={{ width: width > 720 ? 180 : "100%" }}
                />
                <input
                    type="text"
                    placeholder="Referral Code"
                    onChange={updateReferralCode}
                    className="waitlist-form"
                    style={{ width: width > 720 ? 180 : "100%" }}
                />
                <CountryDropdown
                    value={country}
                    onChange={(country) => updateCountry(country)}
                />
            </div>
            <div
                style={{
                    width: 800,
                    margin: "auto",
                    marginTop: width > 720 ? 20 : 0,
                    maxWidth: "100%",
                    padding: width > 720 ? 0 : 30,
                }}
            >
                {user && user.email ? (
                    <Button
                        className="waitlist-button"
                        disabled={true}
                        style={{
                            opacity: 1.0,
                        }}
                    >
                        You're on the waitlist as {user.name}.
                    </Button>
                ) : !processing ? (
                    <Button
                        onClick={insertWaitlist}
                        className="waitlist-button"
                        disabled={email && name && country ? false : true}
                        style={{
                            opacity: email && name && country ? 1.0 : 0.5,
                        }}
                    >
                        REQUEST ACCESS
                    </Button>
                ) : (
                    <Button
                        className="waitlist-button"
                        disabled={true}
                        style={{
                            opacity: email && name && country ? 1.0 : 0.5,
                        }}
                    >
                        Processing
                    </Button>
                )}
            </div>
        </div>
    )
}

const mapStateToProps = (state: { MainReducer: { user: any } }) => {
    return {
        user: state.MainReducer.user,
    }
}

export default connect(mapStateToProps)(WaitlistForm)
