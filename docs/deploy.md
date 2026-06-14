# Deployment

## Recommended: GitHub Pages

This app is static and should work well on GitHub Pages.

1. Create a GitHub repository and push this workspace.
2. In GitHub, open **Settings → Pages**.
3. Set **Source** to **GitHub Actions**.
4. Push to `master` or `main`, or run the `Deploy static site to GitHub Pages` workflow manually.
5. The workflow publishes the repository root, so `index.html` becomes the app entry point and `assets/Payam_Idea.pdf` remains the paper link.

The current local repo has no remote configured yet. Once the remote exists:

```powershell
git remote add origin https://github.com/<owner>/<repo>.git
git push -u origin master
```

## Fallback: GCP Static Hosting

Use Google Cloud Storage for the lowest-maintenance GCP path.

```powershell
gcloud storage buckets create gs://<bucket-name> --location=us-central1
gcloud storage cp --recursive . gs://<bucket-name>
gcloud storage buckets update gs://<bucket-name> --web-main-page-suffix=index.html
gcloud storage buckets add-iam-policy-binding gs://<bucket-name> --member=allUsers --role=roles/storage.objectViewer
```

For a polished public URL, put Cloud CDN or a load balancer in front of the bucket and attach a custom domain. GitHub Pages is simpler unless you specifically need Google Cloud billing, domain controls, or CDN policy.
