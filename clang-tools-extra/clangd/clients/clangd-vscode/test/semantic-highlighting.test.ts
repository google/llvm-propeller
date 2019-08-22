import * as assert from 'assert';
import * as path from 'path';

import * as SM from '../src/semantic-highlighting';

suite('SemanticHighlighting Tests', () => {
  test('Parses arrays of textmate themes.', async () => {
    const themePath =
        path.join(__dirname, '../../test/assets/includeTheme.jsonc');
    const scopeColorRules = await SM.parseThemeFile(themePath);
    const getScopeRule = (scope: string) =>
        scopeColorRules.find((v) => v.scope === scope);
    assert.equal(scopeColorRules.length, 3);
    assert.deepEqual(getScopeRule('a'), {scope : 'a', foreground : '#fff'});
    assert.deepEqual(getScopeRule('b'), {scope : 'b', foreground : '#000'});
    assert.deepEqual(getScopeRule('c'), {scope : 'c', foreground : '#bcd'});
  });
  test('Decodes tokens correctly', () => {
    const testCases: string[] = [
      'AAAAAAABAAA=', 'AAAAAAADAAkAAAAEAAEAAA==',
      'AAAAAAADAAkAAAAEAAEAAAAAAAoAAQAA'
    ];
    const expected = [
      [ {character : 0, scopeIndex : 0, length : 1} ],
      [
        {character : 0, scopeIndex : 9, length : 3},
        {character : 4, scopeIndex : 0, length : 1}
      ],
      [
        {character : 0, scopeIndex : 9, length : 3},
        {character : 4, scopeIndex : 0, length : 1},
        {character : 10, scopeIndex : 0, length : 1}
      ]
    ];
    testCases.forEach((testCase, i) => assert.deepEqual(
                          SM.decodeTokens(testCase), expected[i]));
  });
  test('ScopeRules overrides for more specific themes', () => {
    const rules = [
      {scope : 'variable.other.css', foreground : '1'},
      {scope : 'variable.other', foreground : '2'},
      {scope : 'storage', foreground : '3'},
      {scope : 'storage.static', foreground : '4'},
      {scope : 'storage', foreground : '5'},
      {scope : 'variable.other.parameter', foreground : '6'},
    ];
    const tm = new SM.ThemeRuleMatcher(rules);
    assert.deepEqual(tm.getBestThemeRule('variable.other.cpp').scope,
                     'variable.other');
    assert.deepEqual(tm.getBestThemeRule('storage.static').scope,
                     'storage.static');
    assert.deepEqual(
        tm.getBestThemeRule('storage'),
        rules[2]); // Match the first element if there are duplicates.
    assert.deepEqual(tm.getBestThemeRule('variable.other.parameter').scope,
                     'variable.other.parameter');
    assert.deepEqual(tm.getBestThemeRule('variable.other.parameter.cpp').scope,
                     'variable.other.parameter');
  });
});
