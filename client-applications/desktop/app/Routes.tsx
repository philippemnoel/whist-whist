import React from "react";
import { Switch, Route } from "react-router";
import routes from "./constants/routes.json";
import App from "./containers/App";
import HomePage from "./containers/HomePage";
import CounterPage from "./containers/CounterPage";
import StudiosPage from "./containers/StudiosPage";

export default function Routes() {
    return (
        <App>
            <Switch>
                <Route path={routes.STUDIOS} component={StudiosPage} />
                <Route path={routes.COUNTER} component={CounterPage} />
                <Route path={routes.HOME} component={HomePage} />
            </Switch>
        </App>
    );
}
