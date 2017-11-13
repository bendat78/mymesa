git checkout -- VERSION
git status
git pull origin master
gedit $(git status | grep "von beiden" | sed 's/\tvon beiden geändert:   //')
git commit -a
git push -u mymesa mymesa2
git status
