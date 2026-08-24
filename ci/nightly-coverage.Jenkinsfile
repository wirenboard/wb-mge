// Nightly coverage trigger for wb-mge.
//
// This pipeline compiles nothing and runs no tests of its own. It checks the branch out and, if
// the branch has moved since the last nightly, fires the branch's MAIN build with
// RUN_COVERAGE=true and RUN_E2E=false. Note what that downstream build actually is: the FULL
// main pipeline — lints, unit tests, frontend, firmware build, clang-tidy, the coverage stages,
// and the unconditional 'Upload to S3' at the end, which re-invokes s3_uploader for the same
// revision the day's push build already handed to it (see that stage's comment in the main
// Jenkinsfile; what the uploader then DOES with it lives in a separate repo and could not be
// verified from here, so this says the action, not the outcome). It is
// ~2 h on a shared node and it has side effects. "Just a trigger" describes this file, not its
// consequences.
//
// WHY A SEPARATE JOB, rather than 'triggers { pollSCM(...) }' in the main Jenkinsfile:
// pollSCM compares the SCM's current revision against a polling baseline recorded by the LAST
// BUILD OF THE JOB THAT OWNS THE TRIGGER. The main job already builds every push, so by 02:00
// its baseline IS the branch head — a poll there would find nothing new and never fire, and it
// would fail exactly on the active branches that most deserve nightly coverage. Making it fire
// regardless would mean 'cron' instead of 'pollSCM', which rebuilds every night whether or not
// anything changed: ~2 h of node time per branch per night for a byte-identical result.
// This job builds only when it fires, so its baseline advances only on nightlies, and
// "changed since the last nightly" is what pollSCM computes here.
//
// THAT BASELINE HAS TO BE CREATED, and it is the subtlest thing in this file. A Pipeline job
// records a polling baseline only from an actual SCM checkout registered during a build. In a
// multibranch project the Jenkinsfile itself is read through SCMFileSystem without a workspace,
// which registers NO checkout — so a job that never checks anything out never records a baseline,
// and poll() has nothing to compare against and reports no changes forever. A trigger that can
// never fire, and that never says so. That is why the stage below takes an agent: with a stage
// agent, Declarative performs its implicit 'checkout scm' there, and THAT is what writes the
// baseline. The agent is not for compute — the stage runs no build steps — it exists to make
// the checkout happen, and it costs seconds, not the ~2 h of the build it triggers.
//   => Do NOT add skipDefaultCheckout(), and do not "optimise" this back to 'agent none'.
//      Either change silently disables the nightly.
// This mechanism is derived from how the Pipeline plugins are documented and built, not from
// observation on our server, so verify it on first deployment (see DEPLOYMENT step 3).
//
// DEPLOYMENT — this is meant to live in a SECOND multibranch pipeline over the same repository,
// configured with script path 'ci/nightly-coverage.Jenkinsfile' (the existing multibranch
// project keeps the default 'Jenkinsfile'). Three steps, none of which this file can express:
//
//   1. Property strategy -> "Suppress automatic SCM triggering".
//      Without it the whole design inverts. A multibranch project builds a branch as soon as it
//      sees a new head, from a webhook or a branch scan — so this job would fire on EVERY push,
//      i.e. a ~2 h coverage build per commit, which is the opposite of "nightly". Worse, those
//      indexing builds record baselines of their own, so by 02:00 pollSCM would find nothing new
//      and never fire. Both halves of the design collapse at once. Suppressing automatic
//      triggering leaves the pollSCM trigger below as the only thing that starts this job.
//
//   2. After suppressing, build this job ONCE by hand.
//      A 'triggers' block declared in a Jenkinsfile only takes effect from the SECOND build
//      onwards: Jenkins has to run the pipeline once to learn the trigger exists. Until then
//      nothing is scheduled and the nightly silently never happens. (Same first-build mechanism
//      api_tests/README_API_Tests.md documents for the 'parameters' block.) This registration
//      build is NOT a formality: on the default branch it executes the stage and immediately
//      queues a full ~2 h coverage build on the shared node. Do it when the node is quiet, or
//      abort the downstream build once it has been queued.
//
//   3. Verify the baseline exists, because failure here is silent.
//      After that first build, trigger a poll and read the job's "Git Polling Log". It must say
//      it found (or did not find) changes. If it instead says there is no polling baseline —
//      or the nightly simply never runs on a day with pushes — the checkout in the stage below
//      is not registering, and nothing else in this file will tell you.
//
//   4. Optional, cost only: set the project's clone behaviours (shallow clone, or a reference
//      repository). The implicit checkout below is configured by the JOB, not by this file, and
//      at defaults it is a full clone of this repo for a job that only needs a revision pointer.
//      Polling correctness does not depend on it — this SCM does not require a workspace to
//      poll, so the poll itself is a remote ref query either way.
//
// A deleted branch takes its nightly with it. Nothing here is branch-specific — the target job
// is derived from env.BRANCH_NAME below.
pipeline {
    // No top-level agent: the pipeline holds no node between stages. The one stage that does the
    // work declares its own agent, for the checkout reason explained above.
    agent none

    options {
        // NOTE what this does and does not do. It serialises: a second run queues behind the
        // first and then executes normally — it does NOT cancel it, so on its own it would not
        // stop the downstream being fired twice. What actually prevents a duplicate downstream
        // is Jenkins coalescing queue items: a second request for the same job with an identical
        // parameter set collapses into the one already queued. This option is kept for ordering
        // hygiene only. If outright cancellation is ever wanted, that is abortPrevious: true.
        disableConcurrentBuilds()
        // ~a month of nightlies is plenty of history for a job whose log is three lines.
        buildDiscarder(logRotator(numToKeepStr: '30'))
    }

    triggers {
        // Once a day around 02:00; 'H' spreads the exact minute per job so that every branch's
        // nightly does not poll on the same tick. This POLLS rather than firing unconditionally:
        // a build happens only if the branch head differs from the baseline this job last
        // recorded. See the header for why that check is meaningful here, would be a no-op in
        // the main job, and depends on the stage below actually checking out.
        pollSCM('H 2 * * *')
    }

    stages {
        stage('Trigger coverage build') {
            // Any devenv node will do — this stage runs no build steps. The agent exists so that
            // Declarative's implicit 'checkout scm' runs and records the polling baseline the
            // trigger depends on; see the header. How expensive that checkout is, this file does
            // not get to promise: clone depth is a property of the nightly PROJECT's clone
            // behaviours (DEPLOYMENT step 4), and the default is a full clone — ~54 MiB of
            // packfile for this repo, ~119 MB of .git once on disk. A first build on a fresh
            // node pays that; later builds reusing the workspace are seconds.
            agent { label 'devenv' }
            // Only the default branch gets a nightly. Deliberately NOT "every branch gets its
            // own", which sounds like a feature and is a hazard: 'H 2 * * *' spreads the POLLING
            // minute, not the resulting builds, so several branches' coverage runs would land on
            // the shared node at once, each running the full QEMU suite. That is precisely the
            // ~1.5x CPU oversubscription the main Jenkinsfile's disableConcurrentBuilds comment
            // documents as the cause of false e2e failures — and here it is self-defeating, because
            // a failed QEMU coverage run leaves QEMU_COVERAGE_OK unset, so 'Coverage (combined)'
            // skips and the combined report, the entire point of the nightly, is never produced.
            // To cover a feature branch as well, add it to this list knowingly, having checked
            // what else runs on that node at night.
            //
            // The '!env.BRANCH_NAME' arm is not redundant: it deliberately lets the unset case
            // THROUGH to the steps so the guard there can fail loudly. Without it, running this
            // file outside a multibranch project (where BRANCH_NAME is unset) would make this
            // expression false — ['main'].contains(null) is false — and the stage would be
            // SKIPPED, producing a green build that did nothing, which is precisely the
            // misreading this file is written to prevent. Written as a truthiness test rather
            // than '== null' so that an empty string is caught the same way, matching the guard
            // in the steps below.
            when { expression { !env.BRANCH_NAME || env.BRANCH_NAME in ['main'] } }
            steps {
                script {
                    // Pre-flight. Two assumptions are baked into the path built below, and both
                    // fail as a confusing red build at 2 a.m. rather than as anything legible,
                    // so they are checked and named explicitly:
                    //
                    //  (a) env.BRANCH_NAME exists. It is set by multibranch projects only; run
                    //      this file as a plain pipeline job and it is null, and .replace() on
                    //      null throws a bare NPE naming nothing. Reachable only because the
                    //      'when' above lets null through on purpose.
                    //  (b) branch name -> item name is just '/' -> '%2F'. That holds for the
                    //      branches this repo uses. What was confirmed against the live server,
                    //      by querying the API for a branch that had a slash in its name: the
                    //      fullName it reports contains a LITERAL three-character '%2F' where
                    //      the branch name has the slash — part of the item's real name, not URL
                    //      escaping applied on top — and the web UI escapes that '%' a second
                    //      time to '%252F', which is why the address bar is the wrong place to
                    //      read a job path from. Illustrated with a placeholder name (an example
                    //      of the shape, not a recorded response): branch 'fix/some-branch' would
                    //      be the item 'wirenboard/wb-mge/fix%2Fsome-branch', shown in the browser
                    //      as 'fix%252Fsome-branch'. The rule is NOT universal: branch-api's
                    //      NameMangler rewrites names that are too long or contain unsafe
                    //      characters, and then the mangled item name is not derivable from the
                    //      branch name at all. Hence the existence check below rather than
                    //      blind trust.
                    if (!env.BRANCH_NAME) {
                        error('Nightly coverage: env.BRANCH_NAME is not set. This pipeline is ' +
                              'only meaningful inside a multibranch project (script path ' +
                              'ci/nightly-coverage.Jenkinsfile) — see the DEPLOYMENT note at ' +
                              'the top of this file.')
                    }

                    // 'build job:' takes a JOB PATH, not a branch name. The leading '/' makes it
                    // absolute from the Jenkins root; a relative path would be resolved against
                    // THIS job's folder — the nightly multibranch project — which is not where
                    // the target lives.
                    String targetJob = "/wirenboard/wb-mge/${env.BRANCH_NAME.replace('/', '%2F')}"
                    echo "Nightly coverage: triggering ${targetJob} with RUN_COVERAGE=true, RUN_E2E=false"

                    // RUN_COVERAGE=true, RUN_E2E=false is the coverage-only combination: the
                    // coverage stages run and the plain e2e stage is skipped, so the suite runs
                    // once rather than twice. Both parameters are passed explicitly rather than
                    // relying on the main job's defaults, which are deliberately the opposite.
                    //
                    // wait: false — fire and return. The downstream build takes ~2 h; waiting
                    // would hold this job, its executor and the disableConcurrentBuilds lock open
                    // for all of it to learn a result that nothing here consumes. The downstream
                    // build reports its own status.
                    // Accepted consequence: this job goes green as soon as the request is queued,
                    // so a blue ball here means "coverage was requested", never "coverage is
                    // good" and never "tests passed" — read the downstream build for that.
                    //
                    // The catch exists so that assumption (b) above fails LEGIBLY. 'build' resolves
                    // the job before scheduling, so a wrong or mangled path throws here even with
                    // wait:false — specifically an AbortException("No item named ... found"), which
                    // is why that one type is caught and not Exception: narrowing it this way keeps
                    // a genuine build abort (FlowInterruptedException) reported as ABORTED instead
                    // of being rewritten into a failure. Unwrapped, a bad path surfaces as a red
                    // nightly whose cause has to be dug out of a stack trace; rethrown naming the
                    // exact path tried, it is self-diagnosing. Nothing is swallowed — every failure
                    // still fails the build.
                    try {
                        build job: targetJob, wait: false, parameters: [
                            booleanParam(name: 'RUN_COVERAGE', value: true),
                            booleanParam(name: 'RUN_E2E', value: false),
                        ]
                    } catch (hudson.AbortException e) {
                        error("Nightly coverage: could not trigger job '${targetJob}' " +
                              "(branch '${env.BRANCH_NAME}'). Check that this job path exists and " +
                              "is spelled exactly as Jenkins names the item — a '/' in the branch " +
                              "name appears as a literal '%2F', and branch-api may have mangled " +
                              "long or unsafe names into something not derivable from the branch " +
                              "name. Original error: ${e.message}")
                    }
                }
            }
        }
    }
}
