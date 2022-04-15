<script lang="ts">
  import Box from '../kit/Box.svelte';
  import Button from '../kit/Button.svelte';
  import Icon from './Icon.svelte';
  import Tooltip from './Tooltip.svelte';
  import Input from './Input.svelte';
  import Switch from './Switch.svelte';
  import Radio from './Radio.svelte';
  import Checkbox from './Checkbox.svelte';
  import Dropdown from './Dropdown.svelte';
  import Select from './Select.svelte';
  import type { Option } from './Select.svelte';
  import Notifications from './Notifications.svelte';
  import { sendNotification } from './Notifications.svelte';
  import Table from './table/Table.svelte';
  import Tbody from './table/Tbody.svelte';
  import Thead from './table/Thead.svelte';

  import userFriendsIcon from './icons/userFriends';
  import userIcon from './icons/user';
  import usersIcon from './icons/users';
  import userClockIcon from './icons/userClock';
  import questionIcon from './icons/question';
  import infoIcon from './icons/info';
  import userPlus from './icons/userPlus';
  import userMinus from './icons/userMinus';
  import Popup from './Popup.svelte';
  import PopupLi from './PopupLi.svelte';
  //import Loader from './Loader.svelte';

  let radio = 1;

  function fireNotification() {
    sendNotification({
      text: 'Sample notification',
      timeout: 4000,
    });
  }

  function fireHtmlNotification() {
    sendNotification({
      html: '<b>Html</b> <i>notification</i>',
      timeout: 4000,
    });
  }

  function fireActionNotification() {
    sendNotification({
      text: 'Here is an action',
      actionTitle: 'Got it!',
      onAction: (n) => n.close!(),
    });
  }

  function fireActionWarningNotification() {
    sendNotification({
      text: 'Here is an action',
      actionTitle: 'Got it!',
      onAction: (n) => n.close!(),
      style: 'warning',
      closeable: false,
    });
  }

  function fireActionErrorNotification() {
    sendNotification({
      text: 'Here is an error notification',
      actionTitle: 'Got it!',
      onAction: (n) => n.close!(),
      style: 'error',
      closeable: false,
    });
  }

  const selectOptions = [
    { label: 'Option 1', value: 1 },
    { label: 'Option 2', value: 2 },
    { label: 'Option 3', value: 3 },
  ] as Option[];
  let selectedValue = 1;

  function onPopupSelect(ev: CustomEvent) {
    console.log('on popup select: ', ev);
  }
</script>

<template>
  <Notifications />

  <div class="palette">
    <h2>Popup</h2>

    <Popup>
      <Button>Show popup</Button>
      <ul slot="popup">
        <PopupLi title="One" on:action={onPopupSelect} />
        <PopupLi title="Two two" on:action={onPopupSelect} />
      </ul>
    </Popup>

    <h2>Button with attention mark</h2>
    <Button icon={userClockIcon} attention />
    <Button icon={userClockIcon} componentClass="x" attention />

    <h2>Table</h2>
    <div class="column" style="width:40%; padding:1rem;">
      <Table rounded>
        <Thead>
          <tr>
            <th>Header 1</th>
            <th>Header 2</th>
          </tr>
        </Thead>
        <Tbody>
          <tr>
            <td>cell1</td>
            <td>cell2</td>
          </tr>
          <tr>
            <td>cell1</td>
            <td>cell2</td>
          </tr>
          <tr>
            <td>cell1</td>
            <td>cell2</td>
          </tr>
        </Tbody>
      </Table>

      <Table>
        <Thead>
          <tr>
            <th>Header 1</th>
            <th>Header 2</th>
          </tr>
        </Thead>
        <Tbody>
          <tr>
            <td>cell1</td>
            <td>cell2</td>
          </tr>
          <tr>
            <td>cell1</td>
            <td>cell2</td>
          </tr>
          <tr>
            <td>cell1</td>
            <td>cell2</td>
          </tr>
        </Tbody>
      </Table>
    </div>

    <!-- <h2>Extra buttons</h2>
    <Button icon={userClockIcon} componentClass="onmedia" />
    <Button icon={userClockIcon} loading>Hello</Button>
    <Button icon={userClockIcon} componentClass="x" loading />
    <Button icon={userClockIcon} specialStyle="1" loading>Hello hello</Button>
    <Button icon={userClockIcon} specialStyle="1" componentClass="xx" loading>Hello hello</Button> -->

    <h2>Select</h2>

    <Select bind:value={selectedValue} options={selectOptions} placeholder="Select option" title="Select" />

    <h2>Dropdown</h2>

    <div class="column">
      <Dropdown >
        <Button>Hello dropdown</Button>
        <div slot="dropdown" style="padding:8px">Hello dropdown content</div>
      </Dropdown>
    </div>

    <h2>Notifications</h2>

    <div class="column" style="align-items: stretch;width:40%;">
      <Button on:click={fireNotification}>Fire notification</Button>
      <Button on:click={fireHtmlNotification}>Fire html notification</Button>
      <Button on:click={fireActionNotification}>Fire action notification</Button>
      <Button componentClass="warning x" on:click={fireActionWarningNotification}>Fire warning</Button>
      <Button componentClass="error xx" on:click={fireActionErrorNotification}>Fire error</Button>
    </div>

    <h2>Checkbox</h2>

    <div class="column" style="background:#171d26;padding:4em;">
      <Checkbox>First</Checkbox>
      <Checkbox>Second</Checkbox>
      <Checkbox disabled>Disabled</Checkbox>
      <Checkbox error>Error</Checkbox>
    </div>

    <h2>Radio</h2>

    <div class="column" style="background:#171d26;padding:4em;">
      <Radio bind:group={radio} value={1}>First</Radio>
      <Radio bind:group={radio} value={2}>Second</Radio>
      <Radio bind:group={radio} value={1} disabled>Disabled</Radio>
      <Radio bind:group={radio} value={2} error>Error</Radio>
    </div>

    <h2>Switch</h2>

    <div class="column" style="background:#171d26;padding:4em;">
      <Switch left="Left" checked right="Right" title="Title" componentStyle="margin-top: 1em" />

      <Switch left="Left" right="Right" componentStyle="margin-top: 1em" />

      <Switch left="Left" componentStyle="margin-top: 1em" />

      <Switch right="Right" componentStyle="margin-top: 1em" />

      <Switch title="Hello" componentStyle="margin-top: 1em" />

      <Switch componentStyle="margin-top: 1em" />

      <Switch checked componentStyle="margin-top: 1em" />

      <Switch disabled componentStyle="margin-top: 1em" />

      <Switch disabled checked componentStyle="margin-top: 1em" right="Test" />
    </div>

    <h2>Input</h2>

    <div class="column" style="background:#171d26;padding:4em;">
      <Input
        placeholder="Fill the first name"
        title="Textarea"
        type="textarea"
        icon={userPlus}
        componentStyle="margin-top:1em;"
      />
      <Switch componentStyle="margin-top:1em;" />
      <Input placeholder="Fill the first name" size="normal" componentStyle="margin-top:1em;" flavor="error" />
      <Radio bind:group={radio} value={1}>First</Radio>
      <Radio bind:group={radio} value={2}>Second</Radio>
      <Checkbox>First</Checkbox>
      <Checkbox>Second</Checkbox>
      <Input title="Input with error" icon={userPlus} required disabled tooltip="Disabled" componentStyle="margin-top:1em;" />
      <Input placeholder="Hello" title="Hello" required componentStyle="margin-top:1em;" />
      <Input placeholder="Fill the first name" size="small" componentStyle="margin-top:1em;" />
      <Input placeholder="Fill the first name" size="normal" componentStyle="margin-top:1em;" />
      <Input placeholder="Fill the first name" disabled componentStyle="margin-top:1em;" />
      <Input placeholder="Fill the first name" componentStyle="margin-top:1em;" />
      <Input
        title="Hello"
        placeholder="Fill the first name"
        icon={userPlus}
        tooltip="Optional tooltip"
        componentStyle="margin-top:1em;"
      />
      <Input
        title="Input with error"
        icon={infoIcon}
        tooltip="Input with error"
        flavor="error"
        componentStyle="margin-top:1em;"
      />
      <Input title="Warning input" icon={infoIcon} tooltip="Hello!" flavor="warning" componentStyle="margin-top:1em;" />
      <Input title="Success" icon={infoIcon} tooltip="Hello!" flavor="success" componentStyle="margin-top:1em;" />
    </div>

    <h2>Tooltip</h2>

    <Tooltip>
      <div>
        <Icon icon={questionIcon} />
      </div>
      <div slot="tooltip">Hello hello hello hello 111 222 333</div>
    </Tooltip>

    <Icon icon={infoIcon} tooltip="Yet another tooltip!" />

    <h2>Box</h2>

    <div>
      <Box componentStyle="width:50%" closeable>
        z z z z z z z z z z z z z z z z z z z z z z z z z z z z z z z z z z z z z z z z z z z z z z z z z z z z z z z z
        z z z z z z z z z z z z z z z z z z z z z z z z z z z z z z z z z z
      </Box>

      <Box componentStyle="width:50%;" icon={userIcon} closeable>
        <span>
          Hello Sed ut perspiciatis unde omnis iste natus error sit voluptatem accusantium doloremque laudantium, totam
          rem aperiam, eaque ipsa quae ab illo inventore veritatis et quasi architecto beatae vitae dicta sunt explicabo
        </span>
      </Box>

      <Box componentStyle="width:50%;" componentClass="warning" icon={userIcon} hoverable closeable>
        Hello Sed ut perspiciatis unde omnis iste natus error sit voluptatem accusantium doloremque laudantium, totam
        rem aperiam, eaque ipsa quae ab illo inventore veritatis et quasi architecto beatae vitae dicta sunt explicabo
      </Box>

      <Box componentStyle="width:50%;" componentClass="error" icon={userIcon} hoverable closeable>Hello</Box>
      <Box componentStyle="width:50%;" icon={userIcon} hoverable>Hello hoverable</Box>

      <Box componentStyle="width:50%;" componentClass="primary" hoverable>Hello</Box>
      <Box componentStyle="width:50%;" hoverable>Hello hoverable</Box>
      <Box componentStyle="width:50%;" componentClass="error" hoverable>Hello hoverable</Box>

      <Box>
        Sed ut perspiciatis unde omnis iste natus error sit voluptatem accusantium doloremque laudantium, totam rem
        aperiam, eaque ipsa quae ab illo inventore veritatis et quasi architecto beatae vitae dicta sunt explicabo
        <div style="margin-top:1em;">
          <Button icon={userClockIcon} specialStyle="1">Hello hello</Button>
          <Button icon={userClockIcon}>Hello</Button>
          <Button icon={userClockIcon} disabled>Hello disabled</Button>
          <Button icon={userClockIcon} componentClass="secondary">Hello</Button>
          <Button icon={userClockIcon} componentClass="secondary" disabled>Hello disabled</Button>
        </div>
      </Box>

      <Box closeable>
        Sed ut perspiciatis unde omnis iste natus error sit voluptatem accusantium doloremque laudantium, totam rem
        aperiam, eaque ipsa quae ab illo inventore veritatis et quasi architecto beatae vitae dicta sunt explicabo
        <div style="margin-top:1em;">
          <Button icon={userClockIcon} specialStyle="1">Hello hello</Button>
          <Button icon={userClockIcon}>Hello</Button>
          <Button icon={userClockIcon} componentClass="secondary">Hello</Button>
        </div>
      </Box>
    </div>

    <h2>Buttons special 1</h2>

    <Button icon={userClockIcon} specialStyle="1">x</Button>
    <Button icon={userClockIcon} specialStyle="1">Hello hello</Button>
    <Button icon={userClockIcon} specialStyle="1" disabled>Hello hello disabled</Button>
    <Button icon={userClockIcon} specialStyle="1" componentClass="x secondary">Hello hello</Button>
    <Button icon={userClockIcon} specialStyle="1" componentClass="xx">Hello hello</Button>

    <h2>Icon buttons</h2>
    <Button icon={userClockIcon} />
    <Button icon={userClockIcon} componentClass="x" />
    <Button icon={userClockIcon} componentClass="x" disabled />
    <Button icon={userClockIcon} componentClass="xx" />

    <Button icon={userClockIcon}>Hello</Button>
    <Button icon={userClockIcon} iconClass="icon-text">Hello</Button>
    <Button icon={userFriendsIcon} componentClass="x" iconColor="red">Hello</Button>
    <Button icon={userClockIcon} componentClass="x secondary">Hello</Button>
    <Button icon={userClockIcon} componentClass="x secondary" disabled>Hello</Button>
    <Button icon={userClockIcon} componentClass="xx">Hello</Button>

    <h2>Basic buttons</h2>

    <Button componentClass="s">Small</Button>
    <Button>Open camera</Button>
    <Button componentClass="outline">Outline</Button>
    <Button />
    <Button>Open camera</Button>
    <Button disabled>Open camera disabled</Button>
    <Button componentClass="x">Open camera</Button>
    <Button componentClass="xx secondary">Open camera</Button>
    <Button componentClass="warning">Warning</Button>

    <h2>Icons</h2>
    <div class="cont">
      <Icon icon={userFriendsIcon} />
      <Icon icon={userIcon} />
      <Icon icon={userClockIcon} />
      <Icon icon={userPlus} />
      <Icon icon={userMinus} />
      <Icon icon={usersIcon} />
    </div>

    <h2>Palette</h2>
    <div class="cont">
      <div class="sample" style="background-color: #17212b;">Hello</div>
      <div class="sample" style="background-color: #94ae3f;">Hello</div>
      <!-- <div class="sample" style="background-color: #0ca940;">?????</div> -->
      <div class="sample" style="background-color: #ebebeb;" />
      <div class="sample" style="background-color: #939999;">?????</div>
      <div class="sample" style="background-color: #243754;">Hello</div>
      <div class="sample" style="background-color: #b1091d;">Hello</div>
      <div class="sample" style="background-color: #202733;">Hello</div>
    </div>
  </div>
</template>

<!-- markup (zero or more items) goes here -->
<style lang="scss">
  .palette {
    height: 100vh;
    width: 100vw;
    overflow: auto;
  }
  // .row {
  //   display: flex;
  //   flex-direction: row;
  //   justify-content: flex-start;
  //   align-items: flex-start;
  // }

  .column {
    display: flex;
    flex-direction: column;
    justify-content: flex-start;
    align-items: flex-start;
  }
  .cont {
    align-content: center;
    align-items: center;
    display: flex;
    flex-direction: row;
    flex-wrap: wrap;
    justify-content: left;
    width: 100%;
    margin-top: 2rem;
  }
  .sample {
    display: flex;
    margin: 1rem;
    width: 100px;
    height: 100px;
    align-items: center;
    justify-content: center;
  }
</style>
