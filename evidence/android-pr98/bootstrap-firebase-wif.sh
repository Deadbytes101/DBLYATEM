#!/bin/sh
set -eu

repo='Deadbytes101/DBLYATEM'
repo_id='1317635781'
owner_id='184736772'
environment_name='pr98-firebase-testlab'
evidence_branch='deadbyte/pr98-firebase-android-evidence'
workflow_path='.github/workflows/pr98-firebase-android-evidence.yml'

project_id=${GCP_PROJECT_ID:?GCP_PROJECT_ID is required}
bucket=${FIREBASE_TESTLAB_RESULTS_BUCKET:?FIREBASE_TESTLAB_RESULTS_BUCKET is required}
bucket_location=${GCP_BUCKET_LOCATION:-ASIA-SOUTHEAST1}
service_account_id=${GCP_SERVICE_ACCOUNT_ID:-dblyatem-pr98-testlab}
pool_id=${GCP_WORKLOAD_IDENTITY_POOL_ID:-dblyatem-pr98}
provider_id=${GCP_WORKLOAD_IDENTITY_PROVIDER_ID:-github-pr98}
device_model=${FIREBASE_TESTLAB_DEVICE_MODEL:-}
android_version=${FIREBASE_TESTLAB_ANDROID_VERSION:-}

bucket=${bucket#gs://}
bucket=${bucket%/}
service_account="${service_account_id}@${project_id}.iam.gserviceaccount.com"

for command_name in gcloud gh python3
 do
    command -v "$command_name" >/dev/null 2>&1 || {
        printf 'MISSING COMMAND: %s\n' "$command_name" >&2
        exit 90
    }
 done

gcloud auth list \
    --filter='status:ACTIVE' \
    --format='value(account)' |
    grep -q . || {
        printf '%s\n' 'GCLOUD HAS NO ACTIVE ACCOUNT' >&2
        exit 91
    }

gh auth status >/dev/null 2>&1 || {
    printf '%s\n' 'GITHUB CLI IS NOT AUTHENTICATED' >&2
    exit 92
}

gh repo view "$repo" >/dev/null 2>&1 || {
    printf 'GITHUB REPOSITORY NOT ACCESSIBLE: %s\n' "$repo" >&2
    exit 93
}

gcloud projects describe "$project_id" >/dev/null

gcloud config set project "$project_id" >/dev/null

printf '%s\n' 'ENABLING REQUIRED GOOGLE CLOUD APIS'
gcloud services enable \
    testing.googleapis.com \
    toolresults.googleapis.com \
    firebase.googleapis.com \
    iam.googleapis.com \
    iamcredentials.googleapis.com \
    sts.googleapis.com \
    serviceusage.googleapis.com \
    storage.googleapis.com \
    cloudresourcemanager.googleapis.com \
    --project="$project_id"

if ! gcloud iam service-accounts describe \
        "$service_account" \
        --project="$project_id" >/dev/null 2>&1
then
    gcloud iam service-accounts create \
        "$service_account_id" \
        --project="$project_id" \
        --display-name='DBLYATEM PR98 Firebase Test Lab'
fi

for role in \
    roles/cloudtestservice.testAdmin \
    roles/firebase.analyticsViewer
 do
    gcloud projects add-iam-policy-binding \
        "$project_id" \
        --member="serviceAccount:${service_account}" \
        --role="$role" \
        --condition=None \
        --quiet >/dev/null
 done

if ! gcloud storage buckets describe \
        "gs://${bucket}" \
        --project="$project_id" >/dev/null 2>&1
then
    gcloud storage buckets create \
        "gs://${bucket}" \
        --project="$project_id" \
        --location="$bucket_location" \
        --default-storage-class=STANDARD \
        --uniform-bucket-level-access \
        --public-access-prevention
fi

gcloud storage buckets update \
    "gs://${bucket}" \
    --project="$project_id" \
    --uniform-bucket-level-access \
    --public-access-prevention >/dev/null

if ! gcloud iam workload-identity-pools describe \
        "$pool_id" \
        --project="$project_id" \
        --location=global >/dev/null 2>&1
then
    gcloud iam workload-identity-pools create \
        "$pool_id" \
        --project="$project_id" \
        --location=global \
        --display-name='DBLYATEM PR98 GitHub OIDC'
fi

workflow_ref="${repo}/${workflow_path}@refs/heads/${evidence_branch}"
attribute_condition="assertion.repository_id=='${repo_id}' && assertion.repository_owner_id=='${owner_id}' && assertion.environment=='${environment_name}' && assertion.job_workflow_ref=='${workflow_ref}'"
attribute_mapping='google.subject=assertion.sub,attribute.repository_id=assertion.repository_id,attribute.repository_owner_id=assertion.repository_owner_id,attribute.environment=assertion.environment,attribute.job_workflow_ref=assertion.job_workflow_ref'

if ! gcloud iam workload-identity-pools providers describe \
        "$provider_id" \
        --project="$project_id" \
        --location=global \
        --workload-identity-pool="$pool_id" >/dev/null 2>&1
then
    gcloud iam workload-identity-pools providers create-oidc \
        "$provider_id" \
        --project="$project_id" \
        --location=global \
        --workload-identity-pool="$pool_id" \
        --display-name='DBLYATEM PR98 GitHub provider' \
        --issuer-uri='https://token.actions.githubusercontent.com' \
        --attribute-mapping="$attribute_mapping" \
        --attribute-condition="$attribute_condition"
else
    gcloud iam workload-identity-pools providers update-oidc \
        "$provider_id" \
        --project="$project_id" \
        --location=global \
        --workload-identity-pool="$pool_id" \
        --issuer-uri='https://token.actions.githubusercontent.com' \
        --attribute-mapping="$attribute_mapping" \
        --attribute-condition="$attribute_condition"
fi

project_number=$(gcloud projects describe \
    "$project_id" \
    --format='value(projectNumber)')

principal_set="principalSet://iam.googleapis.com/projects/${project_number}/locations/global/workloadIdentityPools/${pool_id}/attribute.repository_id/${repo_id}"

gcloud iam service-accounts add-iam-policy-binding \
    "$service_account" \
    --project="$project_id" \
    --member="$principal_set" \
    --role='roles/iam.workloadIdentityUser' \
    --condition=None \
    --quiet >/dev/null

provider_name=$(gcloud iam workload-identity-pools providers describe \
    "$provider_id" \
    --project="$project_id" \
    --location=global \
    --workload-identity-pool="$pool_id" \
    --format='value(name)')

environment_json=$(mktemp)
trap 'rm -f "$environment_json"' EXIT HUP INT TERM

python3 - "$environment_json" <<'PY'
import json
import sys

path = sys.argv[1]
payload = {
    "wait_timer": 0,
    "prevent_self_review": False,
    "reviewers": [
        {
            "type": "User",
            "id": 184736772,
        }
    ],
    "deployment_branch_policy": None,
}
with open(path, "w", encoding="utf-8") as handle:
    json.dump(payload, handle)
PY

gh api \
    --method PUT \
    -H 'Accept: application/vnd.github+json' \
    -H 'X-GitHub-Api-Version: 2026-03-10' \
    "repos/${repo}/environments/${environment_name}" \
    --input "$environment_json" >/dev/null

gh variable set GCP_PROJECT_ID \
    --repo "$repo" \
    --env "$environment_name" \
    --body "$project_id"

gh variable set FIREBASE_TESTLAB_RESULTS_BUCKET \
    --repo "$repo" \
    --env "$environment_name" \
    --body "gs://${bucket}"

gh secret set GCP_WORKLOAD_IDENTITY_PROVIDER \
    --repo "$repo" \
    --env "$environment_name" \
    --body "$provider_name"

gh secret set GCP_SERVICE_ACCOUNT \
    --repo "$repo" \
    --env "$environment_name" \
    --body "$service_account"

if [ -n "$device_model" ]; then
    gh variable set FIREBASE_TESTLAB_DEVICE_MODEL \
        --repo "$repo" \
        --env "$environment_name" \
        --body "$device_model"
fi

if [ -n "$android_version" ]; then
    gh variable set FIREBASE_TESTLAB_ANDROID_VERSION \
        --repo "$repo" \
        --env "$environment_name" \
        --body "$android_version"
fi

printf '\n%s\n' 'DBLYATEM PR98 FIREBASE WIF BOOTSTRAP: PASS'
printf 'PROJECT_ID=%s\n' "$project_id"
printf 'PROJECT_NUMBER=%s\n' "$project_number"
printf 'SERVICE_ACCOUNT=%s\n' "$service_account"
printf 'WORKLOAD_IDENTITY_PROVIDER=%s\n' "$provider_name"
printf 'RESULTS_BUCKET=gs://%s\n' "$bucket"
printf 'GITHUB_ENVIRONMENT=%s\n' "$environment_name"
printf 'OIDC_REPOSITORY_ID=%s\n' "$repo_id"
printf 'OIDC_OWNER_ID=%s\n' "$owner_id"
printf 'OIDC_WORKFLOW_REF=%s\n' "$workflow_ref"

if [ -z "$device_model" ] || [ -z "$android_version" ]; then
    printf '\n%s\n' 'DEVICE SELECTION STILL REQUIRED'
    printf '%s\n' 'Inspect the current Test Lab catalog:'
    printf '  gcloud firebase test android models list --project=%s\n' \
        "$project_id"
    printf '  gcloud firebase test android versions list --project=%s\n' \
        "$project_id"
    printf '%s\n' 'Then set environment variables:'
    printf '  gh variable set FIREBASE_TESTLAB_DEVICE_MODEL --repo %s --env %s --body MODEL_ID\n' \
        "$repo" "$environment_name"
    printf '  gh variable set FIREBASE_TESTLAB_ANDROID_VERSION --repo %s --env %s --body VERSION_ID\n' \
        "$repo" "$environment_name"
fi

printf '\n%s\n' 'NO TEST MATRIX WAS STARTED BY THIS SCRIPT'
