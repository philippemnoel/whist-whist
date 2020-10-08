import React, { useState, useEffect } from 'react'
import { connect } from 'react-redux'
import { useSpring, animated } from 'react-spring'
import { history } from 'store/configureStore'
import styles from 'styles/dashboard.css'

import { resetState } from 'store/actions/pure'

const NavTitle = (props: {
    selected: boolean
    text: string
    onClick: any
    style?: any
}) => {
    const { selected, text, onClick, style } = props
    return (
        <div
            className={selected ? styles.selectedNavTitle : styles.navTitle}
            onClick={onClick}
            style={style}
        >
            {text}
        </div>
    )
}

const NavBar = (props: any) => {
    const { dispatch } = props
    const [currentTab, setCurrentTab] = useState('Discover')
    const top =
        currentTab == 'Discover'
            ? '35vh'
            : currentTab == 'Settings'
            ? '42vh'
            : '49vh'

    const selectedTab = useSpring({
        top: top,
    })

    useEffect(() => {
        console.log(top)
    }, [top])

    const handleSignout = () => {
        const storage = require('electron-json-storage')
        storage.set('credentials', { username: '', password: '' })
        dispatch(resetState())
        history.push('/')
    }

    return (
        <div className={styles.navBar}>
            <div className={styles.logoTitle}>Fractal</div>
            <NavTitle
                style={{ marginTop: '33vh' }}
                selected={currentTab == 'Discover'}
                text="Discover"
                onClick={() => setCurrentTab('Discover')}
            />
            <NavTitle
                selected={currentTab == 'Settings'}
                text="Settings"
                onClick={() => setCurrentTab('Settings')}
            />
            <NavTitle
                selected={currentTab == 'Support'}
                text="Support"
                onClick={() => setCurrentTab('Support')}
            />
            <animated.div
                style={selectedTab}
                className={styles.select}
            ></animated.div>
            <button
                type="button"
                onClick={handleSignout}
                className={styles.signoutButton}
                id="signout-button"
                style={{
                    textAlign: 'center',
                }}
            >
                SIGN OUT
            </button>
        </div>
    )
}

const mapStateToProps = (state: any) => {
    return { username: state.MainReducer.auth.username }
}

export default connect(mapStateToProps)(NavBar)
