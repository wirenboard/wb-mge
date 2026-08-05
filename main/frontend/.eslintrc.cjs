module.exports = {
   parser: 'vue-eslint-parser',
   parserOptions: {
      parser: '@typescript-eslint/parser',
      ecmaVersion: 2020,
      sourceType: 'module',
      ecmaFeatures: {
         tsx: true,
         jsx: true
      }
   },
   extends: [
      'plugin:vue/vue3-recommended',
      'plugin:@typescript-eslint/recommended',
      'plugin:@intlify/vue-i18n/base',
   ],
   plugins: ['@stylistic'],
   rules: {
      '@intlify/vue-i18n/no-missing-keys-in-other-locales': 'error',
      camelcase: 0,
      semi: 'off',
      indent: 'off',
      quotes: 'off',
      eqeqeq: ['error', 'always'],
      'no-param-reassign': 'off',
      'max-len': 'off',
      'vue/max-attributes-per-line': 0,
      'no-angle-bracket-type-assertion': 0,
      'no-var-requires': 0,
      'no-return-await': 0,
      '@typescript-eslint/explicit-module-boundary-types': 0,
      'import/no-webpack-loader-syntax': 0,
      'one-var': 0,
      'arrow-parens': 0,
      'generator-star-spacing': 0,
      'no-debugger': 0,
      'no-console': 0,
      'no-extra-semi': 2,
      'space-before-function-paren': 0,
      'no-useless-escape': 0,
      'no-tabs': 0,
      'no-mixed-spaces-and-tabs': 0,
      'new-cap': 0,
      'no-new': 0,
      'prefer-const': 0,
      'vue/no-v-html': 0,
      'lines-between-class-members': 0,
      'no-unused-expressions': 0,
      // @typescript-eslint/no-unused-expressions was added to the recommended set in v8;
      // the base no-unused-expressions rule above is intentionally off, so keep the TS variant off too.
      '@typescript-eslint/no-unused-expressions': 0,
      'no-unused-vars': 0,
      'object-curly-spacing': ['warn', 'always'],
      'vue/singleline-html-element-content-newline': 0,
      'vue/no-mutating-props': 1,
      'no-trailing-spaces': ['error', {}],
      'spaced-comment': ['warn'],
      'no-multi-spaces': ['warn'],
      'object-shorthand': ['error', 'always'],
      'no-multiple-empty-lines': [
         'error',
         {
            max: 1,
            maxEOF: 1,
            maxBOF: 0
         }
      ],
      'func-call-spacing': 'off',
      'brace-style': 'off',
      'comma-dangle': 'off',
      'comma-spacing': 'off',
      '@stylistic/quotes': ['error', 'single', { allowTemplateLiterals: true }],
      '@stylistic/comma-spacing': ['error', { before: false, after: true }],
      '@stylistic/comma-dangle': 'off',
      '@stylistic/brace-style': ['error', '1tbs'],
      '@stylistic/func-call-spacing': ['error', 'never'],
      '@typescript-eslint/array-type': 'off',
      '@typescript-eslint/no-unused-vars': [
         'warn',
         {
            argsIgnorePattern: '^h$',
            // Restore the pre-v8 default: v8 changed caughtErrors from 'none' to 'all',
            // which would newly flag unused catch bindings. Keep prior behavior.
            caughtErrors: 'none'
         }
      ],
      '@typescript-eslint/ban-ts-comment': 0,
      '@typescript-eslint/no-explicit-any': 0,
      '@typescript-eslint/no-this-alias': 0,
      '@typescript-eslint/no-inferrable-types': 0,
      '@stylistic/semi': ['warn'],
      '@stylistic/indent': 0,
      '@stylistic/member-delimiter-style': [
         'error',
         {
            multiline: {
               delimiter: 'semi',
               requireLast: true
            },
            singleline: {
               delimiter: 'semi',
               requireLast: false
            },
            multilineDetection: 'brackets'
         }
      ],
      '@typescript-eslint/explicit-member-accessibility': 0,
      '@typescript-eslint/explicit-function-return-type': 0,
      '@typescript-eslint/no-empty-function': 0,
      '@typescript-eslint/member-ordering': [
         1,
         {
            default: [
               'private-field',
               'protected-field',
               'public-field',
               'field',
               'constructor',
               'private-method',
               'protected-method',
               'public-method',
               'method'
            ]
         }
      ],
      'vue/valid-template-root': 'off',
      'vue/html-indent': 'off',
      'vue/require-default-prop': 'off',
      'vue/html-self-closing': 'off',
      'vue/html-closing-bracket-newline': 'off',
      'vue/multi-word-component-names': 'off'
   }
};
