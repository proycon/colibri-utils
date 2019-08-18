Colibri Utils
===================

This collection of utility currently contains only a single tool:

* **Colibri Lang: Language identification** - Detects in what language parts of a document are. Works on [FoLiA XML](https://proycon.github.io/folia) documents as well as plain text. Currently supports only a limited subset of languages:
    * English, Spanish, Dutch, French, Portuguese, German, Italian, Swedish, Danish (trained on Europarl)
    * Latin (trained on the Clementine Vulgate bible, and some on a few latin works from Project Gutenberg)
    * Historical Dutch:
        * Middle dutch - Trained on Corpus van Reenen/Mulder and Corpus Gysseling
        * Early new dutch  - Trained on Brieven als Buit

Installation
-----------------

This tool is included in our [LaMachine distribution](https://proycon.github.io/LaMachine), which is the easiest and recommended way of obtaining it.

Colibri Utils is written in C++. Building from source is also possible if you
have the expertise, but requires various dependencies, including
[ticcutils](https://github.com/LanguageMachines/ticcutils), [libfolia](https://github.com/LanguageMachines/libfolia), and
[colibri-core](https://github.com/proycon/colibri-core)

Usage
---------

See ``colibri-lang --help``

Methodology
-------------

To identify languages, input tokens are matches against a trained lexicon with
token frequencies, which are loaded in memory. No higher order n-grams are used.

A pseudo-probability is computed for the given sequence of input tokens for
each language, the highest probability wins. A confidence value is computed
simply as the ratio of tokens in the vocabulary divided by the length of the
token sequence. Out of vocabulary words are assigned a very low probability.

Training
----------

New models can easily be trained and added, and are independent of the other
models. Simply train an unindexed patternmodel with [Colibri
Core](https://proycon.github.io/colibri-core) and put the model file and the
class file in your data directory. Ensure the data is tokenised and lower-cased
prior to building a pattern model
([ucto](https://github.com/LanguageMachines/ucto) can do both of this for you).


