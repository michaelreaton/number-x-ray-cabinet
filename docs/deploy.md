# Deployment

## Recommended: GitHub Pages

This app is static and should work well on GitHub Pages.

Repository: <https://github.com/michaelreaton/number-x-ray-cabinet>

Configured Pages URL:

```text
https://michaelreaton.github.io/number-x-ray-cabinet/
```

1. Create a GitHub repository and push this workspace.
2. In GitHub, open **Settings → Pages**.
3. Set **Source** to **GitHub Actions**.
4. Push to `master` or `main`, or run the `Deploy static site to GitHub Pages` workflow manually.
5. The workflow publishes the repository root, so `index.html` becomes the app entry point and `assets/Payam_Idea.pdf` remains the paper link.

The local repo is configured with:

```powershell
git remote add origin https://github.com/michaelreaton/number-x-ray-cabinet.git
git push -u origin main
```

Use pull requests and rebase before pushing updates:

```powershell
git fetch origin
git rebase origin/main
git push
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
