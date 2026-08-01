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

temporary_directory=$(mktemp -d)
trap 'rm -rf "$temporary_directory"' EXIT HUP INT TERM

for command_name in gcloud gh python3 curl
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

billing_enabled=$(gcloud billing projects describe \
    "$project_id" \
    --format='value(billingEnabled)')

case "$billing_enabled" in
    True|true)
        ;;
    *)
        printf 'CLOUD BILLING IS NOT ENABLED: %s\n' "$project_id" >&2
        printf '%s\n' \
            'LINK AN ACTIVE BILLING ACCOUNT BEFORE FIREBASE TEST LAB PROVISIONING' >&2
        exit 94
        ;;
esac

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
    cloudbilling.googleapis.com \
    --project="$project_id"

firebase_project_json="$temporary_directory/firebase-project.json"
firebase_operation_json="$temporary_directory/firebase-operation.json"
firebase_poll_json="$temporary_directory/firebase-poll.json"

access_token=$(gcloud auth print-access-token)

firebase_status=$(curl \
    --silent \
    --show-error \
    --output "$firebase_project_json" \
    --write-out '%{http_code}' \
    --header "Authorization: Bearer ${access_token}" \
    "https://firebase.googleapis.com/v1beta1/projects/${project_id}")

case "$firebase_status" in
    200)
        FIREBASE_PROJECT_JSON="$firebase_project_json" \
        EXPECTED_PROJECT_ID="$project_id" \
        python3 - <<'PY'
import json
import os

with open(os.environ["FIREBASE_PROJECT_JSON"], "r", encoding="utf-8") as handle:
    project = json.load(handle)

if project.get("projectId") != os.environ["EXPECTED_PROJECT_ID"]:
    raise SystemExit("Firebase project ID mismatch")
if project.get("state") not in (None, "ACTIVE"):
    raise SystemExit(f"Firebase project is not active: {project.get('state')!r}")

print("FIREBASE_PROJECT_ALREADY_ENABLED=YES")
PY
        ;;
    404)
        printf '%s\n' 'ADDING FIREBASE SERVICES TO GOOGLE CLOUD PROJECT'

        access_token=$(gcloud auth print-access-token)
        add_status=$(curl \
            --silent \
            --show-error \
            --output "$firebase_operation_json" \
            --write-out '%{http_code}' \
            --request POST \
            --header "Authorization: Bearer ${access_token}" \
            --header 'Content-Type: application/json' \
            --data '{}' \
            "https://firebase.googleapis.com/v1beta1/projects/${project_id}:addFirebase")

        case "$add_status" in
            200|201)
                ;;
            *)
                printf 'FIREBASE addFirebase HTTP STATUS=%s\n' "$add_status" >&2
                cat "$firebase_operation_json" >&2
                printf '%s\n' \
                    'IF TERMS ARE NOT ACCEPTED, OPEN FIREBASE CONSOLE ONCE AND ACCEPT THEM' >&2
                exit 95
                ;;
        esac

        operation_name=$(FIREBASE_OPERATION_JSON="$firebase_operation_json" \
            python3 - <<'PY'
import json
import os

with open(os.environ["FIREBASE_OPERATION_JSON"], "r", encoding="utf-8") as handle:
    operation = json.load(handle)

name = operation.get("name")
if not isinstance(name, str) or not name.startswith("operations/"):
    raise SystemExit("Firebase addFirebase did not return an operation name")
print(name)
PY
        )

        firebase_ready=0
        poll_attempt=0

        while [ "$poll_attempt" -lt 60 ]
        do
            access_token=$(gcloud auth print-access-token)
            poll_status=$(curl \
                --silent \
                --show-error \
                --output "$firebase_poll_json" \
                --write-out '%{http_code}' \
                --header "Authorization: Bearer ${access_token}" \
                "https://firebase.googleapis.com/v1beta1/${operation_name}")

            if [ "$poll_status" != 200 ]; then
                printf 'FIREBASE OPERATION HTTP STATUS=%s\n' "$poll_status" >&2
                cat "$firebase_poll_json" >&2
                exit 96
            fi

            poll_result=$(FIREBASE_POLL_JSON="$firebase_poll_json" \
                EXPECTED_PROJECT_ID="$project_id" \
                python3 - <<'PY'
import json
import os

with open(os.environ["FIREBASE_POLL_JSON"], "r", encoding="utf-8") as handle:
    operation = json.load(handle)

if not operation.get("done", False):
    print("PENDING")
elif "error" in operation:
    print("ERROR")
else:
    response = operation.get("response", {})
    if response.get("projectId") != os.environ["EXPECTED_PROJECT_ID"]:
        print("MISMATCH")
    elif "FirebaseProject" not in str(response.get("@type", "")):
        print("MISMATCH")
    else:
        print("READY")
PY
            )

            case "$poll_result" in
                READY)
                    firebase_ready=1
                    break
                    ;;
                ERROR|MISMATCH)
                    cat "$firebase_poll_json" >&2
                    exit 97
                    ;;
                PENDING)
                    ;;
                *)
                    printf 'UNKNOWN FIREBASE OPERATION STATE=%s\n' \
                        "$poll_result" >&2
                    cat "$firebase_poll_json" >&2
                    exit 98
                    ;;
            esac

            poll_attempt=$((poll_attempt + 1))
            sleep 5
        done

        if [ "$firebase_ready" -ne 1 ]; then
            printf '%s\n' 'FIREBASE PROVISIONING DID NOT REACH READY STATE' >&2
            cat "$firebase_poll_json" >&2
            exit 99
        fi

        printf '%s\n' 'FIREBASE_PROJECT_ENABLED=YES'
        ;;
    *)
        printf 'FIREBASE PROJECT GET HTTP STATUS=%s\n' "$firebase_status" >&2
        cat "$firebase_project_json" >&2
        exit 100
        ;;
esac

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
attribute_condition="assertion.repository_id=='${repo_id}' && assertion.repository_owner_id=='${owner_id}' && assertion.environment=='${environment_name}' && assertion.workflow_ref=='${workflow_ref}'"
attribute_mapping='google.subject=assertion.sub,attribute.repository_id=assertion.repository_id,attribute.repository_owner_id=assertion.repository_owner_id,attribute.environment=assertion.environment,attribute.workflow_ref=assertion.workflow_ref'

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

environment_json="$temporary_directory/environment.json"

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
printf 'BILLING_ENABLED=%s\n' "$billing_enabled"
printf 'FIREBASE_ENABLED=YES\n'
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
