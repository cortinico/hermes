/**
 * Copyright (c) Meta Platforms, Inc. and affiliates.
 *
 * This source code is licensed under the MIT license found in the
 * LICENSE file in the root directory of this source tree.
 *
 * @flow strict-local
 * @format
 */

import {parseForSnapshot, printForSnapshot} from '../__test_utils__/parse';

const parserOpts = {enableExperimentalComponentSyntax: true};
async function printForSnapshotESTree(code: string) {
  return printForSnapshot(code, parserOpts);
}
async function parseForSnapshotESTree(code: string) {
  return parseForSnapshot(code, parserOpts);
}
async function printForSnapshotBabel(code: string) {
  return printForSnapshot(code, {babel: true, ...parserOpts});
}
async function parseForSnapshotBabel(code: string) {
  return parseForSnapshot(code, {babel: true, ...parserOpts});
}

describe('ComponentDeclaration', () => {
  describe('Basic', () => {
    const code = `
      component Foo() {}
    `;

    test('ESTree', async () => {
      expect(await parseForSnapshotESTree(code)).toMatchSnapshot();
      expect(await printForSnapshotESTree(code)).toBe(code.trim());
    });

    test('Babel', async () => {
      expect(await parseForSnapshotBabel(code)).toMatchSnapshot();
      expect(await printForSnapshotBabel(code)).toMatchInlineSnapshot(
        `"function Foo(): React.Node {}"`,
      );
    });
  });

  describe('Complex params', () => {
    const code = `
      component Foo(bar: Bar, baz as boo?: Baz, 'data-bav' as bav: Bav) {}
    `;

    test('ESTree', async () => {
      expect(await printForSnapshotESTree(code)).toBe(code.trim());
      expect(await parseForSnapshotESTree(code)).toMatchSnapshot();
    });

    test('Babel', async () => {
      expect(await parseForSnapshotBabel(code)).toMatchSnapshot();
      expect(await printForSnapshotBabel(code)).toMatchInlineSnapshot(`
        "function Foo({
          bar,
          baz: boo,
          'data-bav': bav
        }: $ReadOnly<{...}>): React.Node {}"
      `);
    });
  });

  describe('default params', () => {
    const code = `
      component Foo(bar?: Bar = '') {}
    `;

    test('ESTree', async () => {
      expect(await printForSnapshotESTree(code)).toBe(code.trim());
      expect(await parseForSnapshotESTree(code)).toMatchSnapshot();
    });

    test('Babel', async () => {
      expect(await parseForSnapshotBabel(code)).toMatchSnapshot();
      expect(await printForSnapshotBabel(code)).toMatchInlineSnapshot(`
        "function Foo({
          bar = ''
        }: $ReadOnly<{...}>): React.Node {}"
      `);
    });
  });

  describe('return type', () => {
    const code = `
      component Foo() renders SpecialType {}
    `;

    test('ESTree', async () => {
      expect(await printForSnapshotESTree(code)).toBe(code.trim());
      expect(await parseForSnapshotESTree(code)).toMatchSnapshot();
    });

    test('Babel', async () => {
      expect(await parseForSnapshotBabel(code)).toMatchSnapshot();
      expect(await printForSnapshotBabel(code)).toMatchInlineSnapshot(
        `"function Foo(): SpecialType {}"`,
      );
    });
  });

  describe('type parameters', () => {
    const code = `
      component Foo<T1, T2>(bar: T1) renders T2 {}
    `;

    test('ESTree', async () => {
      expect(await printForSnapshotESTree(code)).toBe(code.trim());
      expect(await parseForSnapshotESTree(code)).toMatchSnapshot();
    });

    test('Babel', async () => {
      expect(await parseForSnapshotBabel(code)).toMatchSnapshot();
      expect(await printForSnapshotBabel(code)).toMatchInlineSnapshot(`
        "function Foo<T1, T2>({
          bar
        }: $ReadOnly<{...}>): T2 {}"
      `);
    });
  });

  describe('rest params', () => {
    const code = `
      component Foo(...props: Props) {}
    `;

    test('ESTree', async () => {
      expect(await printForSnapshotESTree(code)).toBe(code.trim());
      expect(await parseForSnapshotESTree(code)).toMatchSnapshot();
    });

    test('Babel', async () => {
      expect(await parseForSnapshotBabel(code)).toMatchSnapshot();
      expect(await printForSnapshotBabel(code)).toMatchInlineSnapshot(
        `"function Foo(props: $ReadOnly<{...}>): React.Node {}"`,
      );
    });
  });

  describe('normal and rest params', () => {
    const code = `
      component Foo(param1: string, ...{param2}: Props) {}
    `;

    test('ESTree', async () => {
      expect(await printForSnapshotESTree(code)).toBe(code.trim());
      expect(await parseForSnapshotESTree(code)).toMatchSnapshot();
    });

    test('Babel', async () => {
      expect(await parseForSnapshotBabel(code)).toMatchSnapshot();
      expect(await printForSnapshotBabel(code)).toMatchInlineSnapshot(`
        "function Foo({
          param1,
          ...{
            param2
          }
        }: $ReadOnly<{...}>): React.Node {}"
      `);
    });
  });
  describe('ref param', () => {
    const code = `
      component Foo(ref: Ref) {}
    `;

    test('ESTree', async () => {
      expect(await printForSnapshotESTree(code)).toBe(code.trim());
      expect(await parseForSnapshotESTree(code)).toMatchSnapshot();
    });

    test('Babel', async () => {
      expect(await parseForSnapshotBabel(code)).toMatchSnapshot();
      expect(await printForSnapshotBabel(code)).toMatchInlineSnapshot(`
        "const Foo = React.forwardRef(Foo_withRef);

        function Foo_withRef(_$$empty_props_placeholder$$: $ReadOnly<{...}>, ref: Ref): React.Node {}"
      `);
    });
  });

  describe('ref and normal params', () => {
    const code = `
      component Foo(foo: string, ref: Ref) {}
    `;

    test('ESTree', async () => {
      expect(await printForSnapshotESTree(code)).toBe(code.trim());
      expect(await parseForSnapshotESTree(code)).toMatchSnapshot();
    });

    test('Babel', async () => {
      expect(await parseForSnapshotBabel(code)).toMatchSnapshot();
      expect(await printForSnapshotBabel(code)).toMatchInlineSnapshot(`
        "const Foo = React.forwardRef(Foo_withRef);

        function Foo_withRef({
          foo
        }: $ReadOnly<{...}>, ref: Ref): React.Node {}"
      `);
    });
  });

  describe('ref and normal params default exported', () => {
    const code = `
      export default component Foo(foo: string, ref: Ref) {}
    `;

    test('ESTree', async () => {
      expect(await printForSnapshotESTree(code)).toBe(code.trim());
      expect(await parseForSnapshotESTree(code)).toMatchSnapshot();
    });

    test('Babel', async () => {
      expect(await parseForSnapshotBabel(code)).toMatchSnapshot();
      expect(await printForSnapshotBabel(code)).toMatchInlineSnapshot(`
        "const Foo = React.forwardRef(Foo_withRef);

        function Foo_withRef({
          foo
        }: $ReadOnly<{...}>, ref: Ref): React.Node {}

        export default Foo;"
      `);
    });
  });

  describe('ref and normal params named exported', () => {
    const code = `
      export component Foo(foo: string, ref: Ref) {}
    `;

    test('ESTree', async () => {
      expect(await printForSnapshotESTree(code)).toBe(code.trim());
      expect(await parseForSnapshotESTree(code)).toMatchSnapshot();
    });

    test('Babel', async () => {
      expect(await parseForSnapshotBabel(code)).toMatchSnapshot();
      expect(await printForSnapshotBabel(code)).toMatchInlineSnapshot(`
        "const Foo = React.forwardRef(Foo_withRef);

        function Foo_withRef({
          foo
        }: $ReadOnly<{...}>, ref: Ref): React.Node {}

        export { Foo };"
      `);
    });
  });

  describe('ref and normal params within block', () => {
    const code = `
function A() {
  component Foo(foo: string, ref: Ref) {}
  return Foo;
}
if (true) {
  component Foo(foo: string, ref: Ref) {}
  callSomething(Foo);
}
    `;

    test('ESTree', async () => {
      expect(await printForSnapshotESTree(code)).toBe(code.trim());
      expect(await parseForSnapshotESTree(code)).toMatchSnapshot();
    });

    test('Babel', async () => {
      expect(await parseForSnapshotBabel(code)).toMatchSnapshot();
      expect(await printForSnapshotBabel(code)).toMatchInlineSnapshot(`
        "function A() {
          const Foo = React.forwardRef(Foo_withRef);

          function Foo_withRef({
            foo
          }: $ReadOnly<{...}>, ref: Ref): React.Node {}

          return Foo;
        }

        if (true) {
          const Foo = React.forwardRef(Foo_withRef);

          function Foo_withRef({
            foo
          }: $ReadOnly<{...}>, ref: Ref): React.Node {}

          callSomething(Foo);
        }"
      `);
    });
  });

  describe('ref and normal params within case', () => {
    const code = `
switch (thing) {
  case 1:
    component Foo(foo: string, ref: Ref) {}
    callSomething(Foo);
}
    `;

    test('ESTree', async () => {
      expect(await printForSnapshotESTree(code)).toBe(code.trim());
      expect(await parseForSnapshotESTree(code)).toMatchSnapshot();
    });

    test('Babel', async () => {
      expect(await parseForSnapshotBabel(code)).toMatchSnapshot();
      expect(await printForSnapshotBabel(code)).toMatchInlineSnapshot(`
        "switch (thing) {
          case 1:
            const Foo = React.forwardRef(Foo_withRef);

            function Foo_withRef({
              foo
            }: $ReadOnly<{...}>, ref: Ref): React.Node {}

            callSomething(Foo);
        }"
      `);
    });
  });
});
