export TOKEN :=48f34a3b-a6f4-4eda-8b40-7875b4750f80

# ----- DO NOT MODIFY -----

export COURSE := OS2025
URL := 'http://10.48.6.70:5000/download/submit.sh'

submit:
	@cd $(dir $(abspath $(lastword $(MAKEFILE_LIST)))) && \
	  curl -sSLf '$(URL)' > /dev/null && \
	  curl -sSLf '$(URL)' | bash

git:
	@find ../.shadow/ -maxdepth 1 -type d -name '[a-z]*' | xargs rm -rf
	@cp -r `find .. -maxdepth 1 -type d -name '[a-z]*'` ../.shadow/
	@git add ../.shadow -A --ignore-errors
	@while (test -e .git/index.lock); do sleep 0.1; done
	@(uname -a && uptime) | git commit -F - -q --author='tracer-nju <tracer@nju.edu.cn>' --no-verify --allow-empty
	@sync
