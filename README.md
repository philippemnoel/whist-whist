# Whist Browser & Cloud Tabs Framework

> 💡 **NOTE**
>
> The Whist Browser project was archived in December 2022. The founding team behind Whist has since launched a new project, ParadeDB - https://github.com/paradedb/paradedb.

This repository contains the bulk of the code written by the Whist team for the Whist Browser and its Cloud Tabs framework, following a [monorepo](https://en.wikipedia.org/wiki/Monorepo) structure.

## Repository Status

[![Whist: Build & Deploy](https://github.com/whisthq/whist/actions/workflows/whist-build-and-deploy.yml/badge.svg)](https://github.com/whisthq/whist/actions/workflows/whist-build-and-deploy.yml)

|            Project | Code Coverage                                                                                                                                            |
| -----------------: | :------------------------------------------------------------------------------------------------------------------------------------------------------- |
| `backend/services` | [![codecov](https://codecov.io/gh/whisthq/whist/branch/dev/graph/badge.svg?token=QB0c3c2NBj&flag=backend-services)](https://codecov.io/gh/whisthq/whist) |
|         `protocol` | [![codecov](https://codecov.io/gh/whisthq/whist/branch/dev/graph/badge.svg?token=QB0c3c2NBj&flag=protocol)](https://codecov.io/gh/whisthq/whist)         |
|            Overall | [![codecov](https://codecov.io/gh/whisthq/whist/branch/dev/graph/badge.svg?token=QB0c3c2NBj)](https://codecov.io/gh/whisthq/whist)                       |

## Table of Contents

- [Repository Status](#repository-status)
- [Repository Structure](#repository-structure)
- [Development](#development)
  - [Branch Conventions](#branch-conventions)
  - [Commit Conventions](#commit-conventions)
  - [Hotfixes](#hotfixes)
  - [Promotions](#promotions)
  - [Styling](#styling)
- [Publishing](#publishing)

## Repository Structure

The Whist monorepository contains many subprojects:

| Subrepository                    | Description                                                                             |
| -------------------------------- | --------------------------------------------------------------------------------------- |
| backend/auth0                    | Auth0 is a third-party service which manages authentication and user accounts for us.   |
| backend/services/host-service    | This service runs on EC2 instance hosts and orchestrates mandelbox management.          |
| backend/services/scaling-service | This service is responsible for scaling up/down EC2 instances to run mandelboxes on.    |
| browser/hybrid                   | This contains the Chromium code bundled by `whisthq/brave-core` to create the browser.  |
| host-setup                       | Scripts used to setup EC2 instances for running mandelboxes as Whist-optimized hosts.   |
| mandelboxes                      | Whist-optimized Dockerfiles (known as mandelboxes) that run the applications we stream. |
| protocol                         | The streaming technology API, both client and server, for streaming mandelboxes.        |

For a more in-depth explanation of each subrepository, see that subrepository's README. Note that there is also additional documentation for some subrepos (and other Whist repos) at [docs.whist.com](https://docs.whist.com):

- [docs.whist.com/backend/services](https://docs.whist.com/backend/services)
- [docs.whist.com/protocol](https://docs.whist.com/protocol)

## Development

To get started with development, you will have to clone this repository and navigate to a specific subrepository. On Windows, you will have to install [gitbash](https://git-scm.com/downloads). It is recommended to use [SSH](https://docs.github.com/en/authentication/connecting-to-github-with-ssh/generating-a-new-ssh-key-and-adding-it-to-the-ssh-agent), so that you don't have to type in your GitHub password on every push.

While it is likely that you will work on a feature that touches multiple subrepositories, each subrepository has its own development and styling standards which you should follow, in addition to the usual [Whist Engineering Guidelines](https://www.notion.so/whisthq/Engineering-Guidelines-d8a1d5ff06074ddeb8e5510b4412033b).

To avoid pushing code that does not follow our coding guidelines, we recommend you install pre-commit hooks by running `pip install pre-commit`, followed by `pre-commit install` in the top-level directory. This will install the linting rules specified in `.pre-commit-config.yaml` and prevent you from pushing if your code is not linted.

**NOTE**: Also, once you clone the repo run `git config pull.ff only` inside the repo. This will help prevent pushing unnecessary merge commits (see below).

### Branch Conventions

- `prod` -- This branch is for Stable channel only, do not push to it.
- `staging` -- This branch is for Beta channel only, do not push to it. It gets promoted to `prod` periodically.
- `dev` -- This is our main development branch, PRs should be made to this branch, and it builds the Nightly channel. It gets promoted nightly.
- All other branches are considered feature branches. They should be forked off of `dev` and PR-ed into `dev`.

**Feature branches are named as follows:**

- Project-specific feature branches (i.e. those restricted to a single top-level directory in the repo) are named in the form `<author>/<project>/<feature-verb>`. For instance, a branch authored by Savvy and designed to make the Host Service llama-themed would be called `djsavvy/host-service/add-llama-theme`.
- Broader feature branches that touch multiple projects should be named in the form `<author>/<feature-verb>`.

Note that the last part of the branch name should almost always be an **action** instead of an object (i.e. `add-llama-theme` instead of `llama-theme`). This makes commit logs much easier to parse.

You're free to modify your own branches in any way you want. However, do not attempt to rewrite history on our common branches unless you have a specific reason to do, and the team's approval.

We explicitly disallow merge commits on this repository, and all branches should be either squash-merged or rebase-merged, to avoid messy git histories with lots of "Merged branch `dev` in ....". The commands `git rebase` is your friend, and to avoid accidental merge commits when pulling, you can run `git config pull.ff only` in your local repository.

### Commit Conventions

Clear commit logs are extremely important in a monorepo, due to the number of different active projects that share the git history. Like branch names, commit messages should be descriptive and contain enough context that they make sense _on their own_. Commit messages like "fix", "works", or "oops" can be annoyingly unhelpful. Even something as innocuous-sounding as "staging merge" can be confusing -- are we merging into `staging`, or merging `staging` into another branch? What other branch(es) are involved?

Some good rules of thumb:

- Commit messages, like branch suffixes, should be _actions_ instead of objects.
- Add as much context as you possibly can.
- Forget the "50 characters max" or whatever artificially-low number GitHub and Linus Torvalds start complaining at. It's far more useful to have a clear, contextualized commit message that spills over than an artificially terse mess.

To view a log of only the commits affecting a given file or subdirectory, use the double-hyphen syntax for `git log`: `git log -- <path>`.

### Hotfixes

Eventually, but hopefully rarely, production will be on fire, and we will need to deploy a quick fix ASAP. To do so, branch your changes off of `prod` with the prefix `hotfix/<your-fix>`, and PR your branch directly into `prod`. Once the fix is thoroughly tested, we'll merge it into `prod` and merge `prod` back into `staging` and `staging` back into `dev` to resolve the git history.

### Promotions

Once our `dev` branch is stable and we're ready to bring our new changes out into the world, we'll promote it to `dev`. This can be done by opening a promotion PR via the following URL:

```
https://github.com/whisthq/whist/compare/staging...dev?expand=1&template=promotion.md
```

To open a promotion PR from `staging` to `prod`, simply update the values in the above URL. Once the promotion PR is open and well tested, it can be merged from an engineer with force-push access via:

```
git checkout staging
git merge dev --ff-only
git push -f
```

### Styling

Please refer to a project's subrepository for the associated styling guide. All work done on this monorepository must follow the [Documentation & Code Standards](https://www.notion.so/whisthq/Documentation-Code-Standards-54f2d68a37824742b8feb6303359a597) and the [Engineering Guidelines](https://www.notion.so/whisthq/Engineering-Guidelines-d8a1d5ff06074ddeb8e5510b4412033b).

## Publishing

Each of common branches, `dev`, `staging`, and `prod` get deployed automatically via GitHub Actions to our respective environments on every push to the respective branches, except for `dev` which gets published nightly. This process is quite complex, and is defined in `.github/workflows/whist-build-and-deploy.yml`.

If something goes wrong in the continuous deployment pipeline and any job fails, it is possible to manually trigger a specific job of the workflow via the GitHub Actions console.

Enjoy!
