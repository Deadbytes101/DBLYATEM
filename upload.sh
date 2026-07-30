git add .
msg=""
read -p "commit message: " msg
git commit -m "${msg}"
git push
