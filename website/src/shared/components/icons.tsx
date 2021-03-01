import React from "react"
import { FaLinkedinIn, FaTwitter, FaInstagram, FaMediumM, FaBars} from "react-icons/fa"
import classNames from "classnames"


export const BarsIcon = (props: any) => (
    <button
        type="button"
        {...props}
        className={classNames(
            "bg-none outline-none ",
            props.className
        )}
    >
        <FaBars />
    </button>
)

export const LogoIcon = (props: any) => <a {...props}>{props.children}</a>

export const TwitterIcon = (props: any) => (
    <LogoIcon {...props} id="twitter" href="https://twitter.com/tryfractal">
        <FaTwitter />
    </LogoIcon>
)

export const MediumIcon = (props: any) => (
    <LogoIcon {...props} id="medium" href="https://medium.com/@fractal">
        <FaMediumM />
    </LogoIcon>
)

export const LinkedinIcon = (props: any) => (
    <LogoIcon
        {...props}
        id="linkedin"
        href="https://www.linkedin.com/company/fractal"
    >
        <FaLinkedinIn />
    </LogoIcon>
)

export const InstagramIcon = (props: any) => (
    <LogoIcon
        {...props}
        id="instagram"
        href="https://www.instagram.com/tryfractal"
    >
        <FaInstagram />
    </LogoIcon>
)
