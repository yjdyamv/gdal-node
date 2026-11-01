.PHONY: clean clean-test build build-debug rebuild test test-concurrent test-syntax authors

MOCHA_ARGS=test -R list --v8-expose-gc --require ./test/_common.js

all: build

authors:
	git shortlog -se \
	  | perl -spe 's/^\s+\d+\s+//' \
	  | sed -e '/^BrandonReavis/d' \
	  | sed -e '/brianreavis/d' \
	  | sed -e '/^brandonreavis/d' \
	  > AUTHORS

clean: clean-test
	@rm -rf ./build
	@rm -rf lib/binding

clean-test:
	@rm -f ./test/**/*.aux.xml
	@rm -rf ./test/**/*.tmp*
	@rm -rf ./test/data/**/*.tmp*

build:
	node ./scripts/build.mjs

build-debug:
	node ./scripts/build.mjs --debug

rebuild:
	@make clean
	@make

test: clean-test build
	./node_modules/.bin/mocha --parallel --jobs 4 $(MOCHA_ARGS)
	@make clean-test

test-syntax:
	npm run lint

test-concurrent: clean-test
	node ./node_modules/.bin/_mocha \
		& node ./node_modules/.bin/_mocha \
		& node ./node_modules/.bin/_mocha \
		& node ./node_modules/.bin/_mocha \
		& node ./node_modules/.bin/_mocha \
		& node ./node_modules/.bin/_mocha
	@make clean-test
