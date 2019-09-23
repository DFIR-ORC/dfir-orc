# How to update the fork from mainstream

```bash
git checkout master
git remote add github https://github.com/microsoft/vcpkg.git
git pull github master
git checkout 2019-04-26  # or latest branch
git checkout -b 2019-05-22  # current date
git rebase master  # resolve any conflicts
git push  # update origin
```
