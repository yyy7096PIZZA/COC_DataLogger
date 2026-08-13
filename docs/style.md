<style>
body, .top-bar, .menu {
  background-color: #f1f5f9 !important;
  color: rgb(51, 65, 85) !important;
}

h1, h2, h3, h4, h5 {
  color: #000!important;
}

@media (prefers-color-scheme: dark) {
  body, .top-bar, .menu {
    background-color: #0A0A0C !important;
    color: rgb(209, 213, 219) !important;
  }

  h1, h2, h3, h4, h5 {
    color: #fff !important;
  }
}

/* Thick title font */
.top-bar__title-link {
  font-weight: 600;
}

/* Hide h2 headers on topbar */
.top-bar__items ul:first-child {
  display: none;
}

/* Hide h1 header on sidebar */
.menu > ul:nth-child(2) > li > a,
.menu > ul:nth-child(2) > li li li li li a {
  display: none;
}

.menu li li a {
  padding-left: 1rem;
}

.menu li li li a {
  padding-left: 2rem;
}

.menu li li li li a {
  padding-left: 3rem;
}

/* Wider content width */
.content__inner {
  max-width: 80ch !important;
}

/* Add dot next to the number */
.content ol li:before {
  content: counter(count)'.' !important
}

/* Skip number increment on ul */
.content ul li {
  counter-increment: none;
}

/* Better bullet for li */
.content ul li:before {
  content: '◦' !important
}

/* Better margins for list */
.content ul, .content ol {
  margin-top: 0.5rem;
  margin-bottom: 0.5rem;
}

.content ul li, .content ol li {
  margin-left: 1.5rem;
  padding-left: 1rem;
}

.content li ul li, .content li ol li {
  margin-left: 0.5rem;
}

/* limit image width for vertical images */
.content img {
  max-height: 700px;
}

/* Slack style inline code block */
.content code {
  color: #e8912d !important;
}

/* Github style code block */
.content pre code {
  font-size: 0.9rem;
  line-height: 1.2rem;
  color: #ccc !important;
}

/* Same margin as outside details */
.content details,
.content details p {
  margin-top: 1rem;
}

/* Cursor pointer for details expand */
.content details summary {
  cursor: pointer;
  text-decoration: underline;
  text-decoration-thickness: 1px;
  text-underline-offset: 0.1875rem;
}

/* More margin at header bottom */
.content h1, .content h2, .content h3, .content h4 {
  margin-bottom: 1rem;
}

.content h5 {
  font-size: 0.9rem;
  font-style: italic;
}

/* Github style table */
table {
  width: 100%;
  border-collapse: collapse;
  border-spacing: 0;
  background: #f1f5f9;
  color: #111;
}

th, td {
  padding: 7px 12px;
  border: 1px solid #d0d7de;
}

thead th {
  background: #f2f2f2;
  color: #0a0a0a !important;
  font-weight: 600;
  border-bottom-width: 2px;
}

tbody tr:nth-child(even) {
  background: rgba(0, 0, 0, .03);
}

tbody tr:hover {
  background: rgba(0, 0, 0, .06);
}

caption {
  caption-side: bottom;
  padding-top: .5em;
  font-size: .875rem;
  color: #666;
  text-align: left;
}

@media (prefers-color-scheme: dark) {
  table {
    background: #09090b;
    color: #f0f3f6;
  }

  th, td {
    border-color: #333;
  }

  thead th {
    background: #0d0d0d;
    color: #fff !important;
    border-bottom-color: #3d3d3d;
  }

  tbody tr:nth-child(even) {
    background: rgba(255, 255, 255, .04);
  }

  tbody tr:hover {
    background: rgba(255, 255, 255, .08);
  }

  caption {
    color: #9aa0a6;
  }
}

/*
The MIT License (MIT)

Copyright (c) Yuan Qing Lim

Permission is hereby granted, free of charge, to any person obtaining a copy of this software and associated documentation files (the "Software"), to deal in the Software without restriction, including without limitation the rights to use, copy, modify, merge, publish, distribute, sublicense, and/or sell copies of the Software, and to permit persons to whom the Software is furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in all copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
*/

</style>
