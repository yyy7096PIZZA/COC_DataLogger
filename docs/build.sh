#! /bin/bash

echo "$(cat package.json | jq '."single-page-markdown-website".links[1] = {"text":"Control Hub", "url": "/"}')" > package.json
echo "$(cat package.json | jq '."single-page-markdown-website".links[2] = {"text":"GitHub", "url": "https://github.com/CreatOurCar/monolith"}')" > package.json

echo "$(cat package.json | jq '."single-page-markdown-website".links[0] = {"text":"English", "url": "index.html"}')" > package.json
npx --yes -- single-page-markdown-website docs_ko.md style.md --output .
mv index.html ko.html

echo "$(cat package.json | jq '."single-page-markdown-website".links[0] = {"text":"한국어", "url": "ko.html"}')" > package.json
npx --yes -- single-page-markdown-website docs_en.md style.md --output .

echo "$(cat package.json | jq 'del(."single-page-markdown-website".links[0])')" > package.json

#! /bin/bash

echo "$(cat package.json | jq '."single-page-markdown-website".links[1] = {"text":"Control Hub", "url": "/"}')" > package.json
echo "$(cat package.json | jq '."single-page-markdown-website".links[2] = {"text":"GitHub", "url": "https://github.com/CreatOurCar/monolith"}')" > package.json

echo "$(cat package.json | jq '."single-page-markdown-website".links[0] = {"text":"English", "url": "index.html"}')" > package.json
npx --yes -- single-page-markdown-website docs_ko.md style.md --output .
mv index.html ko.html

echo "$(cat package.json | jq '."single-page-markdown-website".links[0] = {"text":"한국어", "url": "ko.html"}')" > package.json
npx --yes -- single-page-markdown-website docs_en.md style.md --output .

echo "$(cat package.json | jq 'del(."single-page-markdown-website".links[0])')" > package.json
