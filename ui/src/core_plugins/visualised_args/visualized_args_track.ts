// Copyright (C) 2024 The Android Open Source Project
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//      http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

import m from 'mithril';

import {Actions} from '../../common/actions';
import {globals} from '../../frontend/globals';
import {Button} from '../../widgets/button';
import {Icons} from '../../base/semantic_icons';
import {ThreadSliceTrack} from '../thread_slice/thread_slice_track';
import {uuidv4Sql} from '../../base/uuid';
import {NewTrackArgs} from '../../frontend/track';
import {Disposable, DisposableCallback} from '../../base/disposable';

// Similar to a SliceTrack, but creates a view
export class VisualisedArgsTrack extends ThreadSliceTrack {
  private viewName: string;

  constructor(
    args: NewTrackArgs,
    trackId: number,
    maxDepth: number,
    private argName: string,
  ) {
    const uuid = uuidv4Sql();
    const escapedArgName = argName.replace(/[^a-zA-Z]/g, '_');
    const viewName = `__arg_visualisation_helper_${escapedArgName}_${uuid}_slice`;
    super(args, trackId, maxDepth, viewName);
    this.viewName = viewName;
  }

  async onInit(): Promise<Disposable> {
    // Create the helper view - just one which is relevant to this slice
    await this.engine.query(`
        create view ${this.viewName} as
        with slice_with_arg as (
          select
            slice.id,
            slice.track_id,
            slice.ts,
            slice.dur,
            slice.thread_dur,
            NULL as cat,
            args.display_value as name
          from slice
          join args using (arg_set_id)
          where args.key='${this.argName}'
        )
        select
          *,
          (select count()
          from ancestor_slice(s1.id) s2
          join slice_with_arg s3 on s2.id=s3.id
          ) as depth
        from slice_with_arg s1
        order by id;
    `);

    return new DisposableCallback(() => {
      this.engine.tryQuery(`drop view ${this.viewName}`);
    });
  }

  getTrackShellButtons(): m.Children {
    return m(Button, {
      onclick: () => {
        // This behavior differs to the original behavior a little.
        // Originally, hitting the close button on a single track removed ALL
        // tracks with this argName, whereas this one only closes the single
        // track.
        // This will be easily fixable once we transition to using dynamic
        // tracks instead of this "initial state" approach to add these tracks.
        globals.dispatch(Actions.removeTracks({trackKeys: [this.trackKey]}));
      },
      icon: Icons.Close,
      title: 'Close',
      compact: true,
    });
  }
}
