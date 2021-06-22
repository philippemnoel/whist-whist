**High-Level Description of Features**


**List of PRs that made it into this promotion**


**Testing Checklist Before Approving**

In order for this PR to be merged, all of the following items need to be tested and checked. Make sure to perform these tests from the correct packaged app (on the `dev` > `staging` promotion, use the `dev` app, and on the `staging` > `prod` promotion, use the `staging` app). Everyone is encouraged to do these tests, the more people that test the better! If you don't have them already installed, you can download the applications from the following links:

[Dev macOS Application](https://fractal-chromium-macos-dev.s3.amazonaws.com/Fractal.dmg)
[Staging macOS Application](https://fractal-chromium-macos-staging.s3.amazonaws.com/Fractal.dmg)

[Dev Windows Application](https://fractal-chromium-windows-dev.s3.amazonaws.com/Fractal.dmg)
[Staging Windows Application](https://fractal-chromium-windows-staging.s3.amazonaws.com/Fractal.dmg)

- [ ] Fractal successfully auto-updates
- [ ] Fractal successfully launches
- [ ] Typing "where am I" in Google shows that I'm connected to the closest datacenter
- [ ] Copy text from Fractal to local clipboard
- [ ] Copy text from local clipboard to Fractal
- [ ] Copy image from Fractal to local clipboard
- [ ] Copy image from local clipboard to Fractal
- [ ] Do 30 seconds of type racer, no repeated characters, lag, etc.
- [ ] Watch a 1 minute youtube video with no video or audio stutters
- [ ] Go to Figma and verify pinch-to-zoom works
- [ ] Verify that smooth two-finger scrolling works
- [ ] Log into any website, close Fractal, and re-open. I am still logged in.
- [ ] Signout button on tray works
- [ ] Quit button on tray works
- [ ] Session logs successfully uploaded to Amplitude
- [ ] Protocol logs successfully uploaded to AWS
