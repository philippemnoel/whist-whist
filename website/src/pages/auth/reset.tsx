import React from "react"
import { connect } from "react-redux"
import { Redirect, useLocation } from "react-router-dom"

import { updateAuthFlow } from "store/actions/auth/pure"

import Header from "shared/components/header"
import ResetView from "pages/auth/views/resetView"

import "styles/auth.css"

const Reset = (props: any) => {
    const { dispatch } = props
    const search = useLocation().search
    const token = search.substring(1, search.length)
    const valid_token = token && token.length >= 1

    if (!valid_token) {
        dispatch(
            updateAuthFlow({
                mode: "Forgot",
            })
        )
        return <Redirect to="/auth/bypass" />
    } else {
        return (
            <div className="fractalContainer">
                <Header dark={false} />
                <ResetView token={token} validToken={valid_token} />
            </div>
        )
    }
}

export default connect()(Reset)
