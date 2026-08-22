# Git Push Instructions - Eremosisima Credentials

## Stored Credentials
- Username: `Eremosisima`
- Credential target: `git:https://Eremosisima@github.com`
- Stored in Windows Credential Manager

## Steps to Push to GitHub

### 1. Get token from stored credentials
```powershell
echo "protocol=https
host=github.com
username=Eremosisima" | git credential fill
```
This outputs `password=ghp_...` (the GitHub PAT).

### 2. Create repo and push
```powershell
$token = (echo "protocol=https`nhost=github.com`nusername=Eremosisima" | git credential fill | Select-String "password=") -replace "password=",""
$env:GH_TOKEN = $token
gh repo create <REPO_NAME> --public --source=. --push --description "..."
```

### 3. Or push to existing repo
```powershell
$token = (echo "protocol=https`nhost=github.com`nusername=Eremosisima" | git credential fill | Select-String "password=") -replace "password=",""
git remote set-url origin https://Eremosisima:${token}@github.com/Eremosisima/<REPO>.git
git push -u origin master
```

## Quick One-Liner (recommended)
```powershell
$env:GH_TOKEN = ((echo "protocol=https`nhost=github.com`nusername=Eremosisima" | git credential fill) -split "`n" | Select-String "password=") -replace "password=",""; gh repo create <NAME> --public --source=. --push
```

## Notes
- Token is a GitHub Personal Access Token (classic)
- Credential Manager handles token rotation automatically
- If token expires, run `gh auth login` to refresh
