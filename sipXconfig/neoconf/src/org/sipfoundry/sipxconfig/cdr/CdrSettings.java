/**
 *
 *
 * Copyright (c) 2012 eZuce, Inc. All rights reserved.
 * Contributed to SIPfoundry under a Contributor Agreement
 *
 * This software is free software; you can redistribute it and/or modify it under
 * the terms of the Affero General Public License (AGPL) as published by the
 * Free Software Foundation; either version 3 of the License, or (at your option)
 * any later version.
 *
 * This software is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS
 * FOR A PARTICULAR PURPOSE. See the GNU Affero General Public License for more
 * details.
 */
package org.sipfoundry.sipxconfig.cdr;

import org.sipfoundry.sipxconfig.setting.PersistableSettings;
import org.sipfoundry.sipxconfig.setting.Setting;

public class CdrSettings extends PersistableSettings {

    public int getAgentPort() {
        return (Integer) getSettingTypedValue("callresolver/SIP_CALLRESOLVER_AGENT_PORT");
    }

    public int getPostresPort() {
        return 5432; // not configurable
    }

    @Override
    protected Setting loadSettings() {
        return getModelFilesContext().loadModelFile("sipxcallresolver/sipxcallresolver.xml");
    }

    @Override
    public String getBeanId() {
        return "cdrSettings";
    }
}