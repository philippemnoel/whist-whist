import React from "react"
import PuffLoader from "react-spinners/PuffLoader"

// in this page we'll have a bunch of small components that get reused to display loading animations
// (might change later to include other animations and visuals...)

/**
 * A puff loader that takes over the middle of the page.
 * @param props unused
 */
export const PuffAnimation = (props: any) => (
    <div
        style={{
            position: "relative",
            width: "100%",
            height: "100%",
            marginTop: "30%",
        }}
    >
        <PuffLoader
            css="position: absolute; top: 50%; left: 50%; transform: translate(-50%, -50%);"
            size={75}
        />
    </div>
)
